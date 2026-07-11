#include "AppleForegroundMask.h"
#include <iostream>
#include <algorithm>

extern "C"
{
	DLLEXPORT
	void
	FillTOPPluginInfo(TD::TOP_PluginInfo* info)
	{
		if (!info->setAPIVersion(TD::TOPCPlusPlusAPIVersion))
		{
			return;
		}

		info->executeMode = TD::TOP_ExecuteMode::CPUMem;

		info->customOPInfo.opType->setString("Appleforegroundmask");
		info->customOPInfo.opLabel->setString("Apple Foreground Mask");
		info->customOPInfo.opIcon->setString("AFM");

		info->customOPInfo.authorName->setString("Max Mainio Beidler");
		info->customOPInfo.authorEmail->setString("beidler.max@gmail.com");

		info->customOPInfo.minInputs = 1;
		info->customOPInfo.maxInputs = 1;
	}

	DLLEXPORT
	TD::TOP_CPlusPlusBase*
	CreateTOPInstance(const TD::OP_NodeInfo* info, TD::TOP_Context* context)
	{
		return new AppleForegroundMask(info, context);
	}

	DLLEXPORT
	void
	DestroyTOPInstance(TD::TOP_CPlusPlusBase* instance, TD::TOP_Context* context)
	{
		delete (AppleForegroundMask*)instance;
	}
};

AppleForegroundMask::AppleForegroundMask(const TD::OP_NodeInfo* info, TD::TOP_Context* context)
	: myContext(context),
	  myExecuteCount(0)
{
}

AppleForegroundMask::~AppleForegroundMask()
{
	if (inputPool)
	{
		CVPixelBufferPoolRelease(inputPool);
	}
}

void
AppleForegroundMask::getGeneralInfo(
	TD::TOP_GeneralInfo* ginfo,
	const TD::OP_Inputs* inputs,
	void* reserved1)
{
	ginfo->cookEveryFrame = false;
	ginfo->cookEveryFrameIfAsked = false;
}

void
AppleForegroundMask::ensurePool(size_t width, size_t height)
{
	if (inputPool &&
		inputPoolW == width &&
		inputPoolH == height)
	{
		return;
	}

	if (inputPool)
	{
		CVPixelBufferPoolRelease(inputPool);
		inputPool = nullptr;
	}

	NSDictionary* poolAttrs =
	@{
		(NSString*)kCVPixelBufferPoolMinimumBufferCountKey : @3
	};

	NSDictionary* bufferAttrs =
	@{
		(NSString*)kCVPixelBufferWidthKey : @(width),
		(NSString*)kCVPixelBufferHeightKey : @(height),
		(NSString*)kCVPixelBufferPixelFormatTypeKey :
			@(kCVPixelFormatType_32BGRA),
		(NSString*)kCVPixelBufferIOSurfacePropertiesKey : @{}
	};

	CVPixelBufferPoolCreate(
		kCFAllocatorDefault,
		(__bridge CFDictionaryRef)poolAttrs,
		(__bridge CFDictionaryRef)bufferAttrs,
		&inputPool
	);

	inputPoolW = width;
	inputPoolH = height;
}

void
AppleForegroundMask::runForegroundMask(CVPixelBufferRef inputBuffer)
{
	@autoreleasepool
	{
		VNGenerateForegroundInstanceMaskRequest* request =
			[[VNGenerateForegroundInstanceMaskRequest alloc] init];

		VNImageRequestHandler* handler =
			[[VNImageRequestHandler alloc]
				initWithCVPixelBuffer:inputBuffer
				options:@{}];

		NSError* error = nil;

		[handler performRequests:@[request] error:&error];

		if (!error && request.results.count > 0)
		{
			VNInstanceMaskObservation* observation =
				(VNInstanceMaskObservation*)request.results.firstObject;

			CVPixelBufferRef maskBuf =
				[observation generateMaskForInstances:observation.allInstances
												error:&error];

			if (!error && maskBuf)
			{
				CVPixelBufferLockBaseAddress(
					maskBuf,
					kCVPixelBufferLock_ReadOnly
				);

				size_t mw = CVPixelBufferGetWidth(maskBuf);
				size_t mh = CVPixelBufferGetHeight(maskBuf);

				void* baseAddress =
					CVPixelBufferGetBaseAddress(maskBuf);

				size_t maskBPR =
					CVPixelBufferGetBytesPerRow(maskBuf);

				OSType pixelFormat =
					CVPixelBufferGetPixelFormatType(maskBuf);

				{
					std::lock_guard<std::mutex> lock(maskMutex);

					cachedMaskW = mw;
					cachedMaskH = mh;
					cachedMaskData.resize(mw * mh);

					for (size_t y = 0; y < mh; ++y)
					{
						float* dstRow =
							cachedMaskData.data() + y * mw;

						uint8_t* srcRowBytes =
							(uint8_t*)baseAddress + y * maskBPR;

						if (pixelFormat == kCVPixelFormatType_OneComponent32Float)
						{
							float* srcRow =
								(float*)srcRowBytes;

							for (size_t x = 0; x < mw; ++x)
							{
								dstRow[x] =
									std::clamp(srcRow[x], 0.0f, 1.0f);
							}
						}
						else if (pixelFormat == kCVPixelFormatType_OneComponent8)
						{
							uint8_t* srcRow =
								(uint8_t*)srcRowBytes;

							for (size_t x = 0; x < mw; ++x)
							{
								dstRow[x] =
									(float)srcRow[x] / 255.0f;
							}
						}
						else if (pixelFormat == kCVPixelFormatType_32BGRA)
						{
							uint8_t* srcRow =
								(uint8_t*)srcRowBytes;

							for (size_t x = 0; x < mw; ++x)
							{
								// BGRA layout: B, G, R, A.
								// Use alpha as the mask.
								dstRow[x] =
									(float)srcRow[x * 4 + 3] / 255.0f;
							}
						}
						else
						{
							// Fallback: read the first byte as normalized data.
							uint8_t* srcRow =
								(uint8_t*)srcRowBytes;

							for (size_t x = 0; x < mw; ++x)
							{
								dstRow[x] =
									(float)srcRow[x] / 255.0f;
							}
						}
					}
				}

				CVPixelBufferUnlockBaseAddress(
					maskBuf,
					kCVPixelBufferLock_ReadOnly
				);

				hasCachedMask.store(true);
			}
		}

		CVPixelBufferRelease(inputBuffer);
	}
}

void
AppleForegroundMask::setupParameters(
	TD::OP_ParameterManager* manager,
	void* reserved1)
{
	TD::OP_StringParameter sp;

	sp.name = "Resolution";
	sp.label = "Output Resolution";
	sp.defaultValue = "Matchinput";
	sp.page = "Foreground Mask";

	const char* names[] =
	{
		"Matchinput",
		"Visionnative"
	};

	const char* labels[] =
	{
		"Match Input",
		"Vision Native"
	};

	manager->appendMenu(sp, 2, names, labels);
}

void
AppleForegroundMask::pulsePressed(
	const char* name,
	void* reserved1)
{
}

void
AppleForegroundMask::execute(
	TD::TOP_Output* output,
	const TD::OP_Inputs* inputs,
	void* reserved1)
{
	myExecuteCount++;

	int resolutionMode = inputs->getParInt("Resolution");

	if (inputs->getNumInputs() <= 0)
	{
		return;
	}

	const TD::OP_TOPInput* input =
		inputs->getInputTOP(0);

	if (!input)
	{
		return;
	}

	TD::OP_TOPInputDownloadOptions downloadOpts;
	downloadOpts.pixelFormat = TD::OP_PixelFormat::BGRA8Fixed;
	downloadOpts.verticalFlip = true;

	TD::OP_SmartRef<TD::OP_TOPDownloadResult> downloadResult =
		input->downloadTexture(downloadOpts, nullptr);

	if (!downloadResult)
	{
		return;
	}

	size_t width =
		downloadResult->textureDesc.width;

	size_t height =
		downloadResult->textureDesc.height;

	ensurePool(width, height);

	CVPixelBufferRef poolBuffer = nullptr;

	CVReturn cvResult =
		CVPixelBufferPoolCreatePixelBuffer(
			nullptr,
			inputPool,
			&poolBuffer
		);

	if (cvResult == kCVReturnSuccess && poolBuffer)
	{
		CVPixelBufferLockBaseAddress(poolBuffer, 0);

		uint8_t* destData =
			(uint8_t*)CVPixelBufferGetBaseAddress(poolBuffer);

		size_t destBPR =
			CVPixelBufferGetBytesPerRow(poolBuffer);

		uint8_t* srcData =
			(uint8_t*)downloadResult->getData();

		size_t srcBPR = width * 4;

		for (size_t y = 0; y < height; ++y)
		{
			memcpy(
				destData + y * destBPR,
				srcData + y * srcBPR,
				srcBPR
			);
		}

		CVPixelBufferUnlockBaseAddress(poolBuffer, 0);

		hasCachedMask.store(false);

		runForegroundMask(poolBuffer);
	}

	if (!hasCachedMask.load())
	{
		return;
	}

	std::lock_guard<std::mutex> lock(maskMutex);

	size_t maskW = cachedMaskW;
	size_t maskH = cachedMaskH;

	float* maskData =
		cachedMaskData.data();

	TD::OP_TextureDesc suggestedDesc;
	output->getSuggestedOutputDesc(&suggestedDesc, nullptr);

	size_t commonW =
		(suggestedDesc.width > 0) ? suggestedDesc.width : width;

	size_t commonH =
		(suggestedDesc.height > 0) ? suggestedDesc.height : height;

	// Match Input uses TouchDesigner's Common page resolution result.
	// Vision Native bypasses resizing and outputs the raw Vision mask resolution.
	size_t outW =
		(resolutionMode == 1) ? maskW : commonW;

	size_t outH =
		(resolutionMode == 1) ? maskH : commonH;

	bool sameSize =
		(maskW == outW && maskH == outH);

	uint32_t fpScaleX = 0;
	uint32_t fpScaleY = 0;

	if (!sameSize)
	{
		fpScaleX =
			(uint32_t)((maskW << 16) / outW);

		fpScaleY =
			(uint32_t)((maskH << 16) / outH);
	}

	uint64_t outSize =
		outW * outH * sizeof(float);

	TD::OP_SmartRef<TD::TOP_Buffer> outBuffer =
		myContext->createOutputBuffer(
			outSize,
			TD::TOP_BufferFlags::None,
			nullptr
		);

	float* outData =
		(float*)outBuffer->data;

	if (sameSize)
	{
		for (size_t y = 0; y < outH; ++y)
		{
			float* maskRow =
				maskData + (y * maskW);

			float* outRow =
				outData + (y * outW);

			memcpy(
				outRow,
				maskRow,
				outW * sizeof(float)
			);
		}
	}
	else
	{
		for (size_t y = 0; y < outH; ++y)
		{
			float* outRow =
				outData + (y * outW);

			uint32_t fpY = y * fpScaleY;

			size_t y0 = fpY >> 16;
			size_t y1 =
				(y0 < maskH - 1) ? y0 + 1 : y0;

			uint32_t wy =
				(fpY & 0xFFFF) >> 8;

			float* row0 =
				maskData + (y0 * maskW);

			float* row1 =
				maskData + (y1 * maskW);

			uint32_t currentFpX = 0;

			for (size_t x = 0; x < outW; ++x)
			{
				size_t x0 = currentFpX >> 16;
				size_t x1 =
					(x0 < maskW - 1) ? x0 + 1 : x0;

				uint32_t wx =
					(currentFpX & 0xFFFF) >> 8;

				float fx =
					(float)wx / 255.0f;

				float fy =
					(float)wy / 255.0f;

				float v00 = row0[x0];
				float v01 = row0[x1];
				float v10 = row1[x0];
				float v11 = row1[x1];

				float top =
					v00 * (1.0f - fx) + v01 * fx;

				float bot =
					v10 * (1.0f - fx) + v11 * fx;

				float val =
					top * (1.0f - fy) + bot * fy;

				outRow[x] =
					std::clamp(val, 0.0f, 1.0f);

				currentFpX += fpScaleX;
			}
		}
	}

	TD::TOP_UploadInfo uploadInfo;

	uploadInfo.textureDesc = suggestedDesc;

	uploadInfo.textureDesc.width =
		(uint32_t)outW;

	uploadInfo.textureDesc.height =
		(uint32_t)outH;

	uploadInfo.textureDesc.texDim =
		TD::OP_TexDim::e2D;

	uploadInfo.textureDesc.pixelFormat =
		TD::OP_PixelFormat::Mono32Float;

	uploadInfo.firstPixel =
		TD::TOP_FirstPixel::TopLeft;

	output->uploadBuffer(
		&outBuffer,
		uploadInfo,
		nullptr
	);
}