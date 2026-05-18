#include "AppleSubjectMask.h"
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

		info->customOPInfo.opType->setString("Applesubjectmask");
		info->customOPInfo.opLabel->setString("Apple Subject Mask");
		info->customOPInfo.opIcon->setString("ASM");

		info->customOPInfo.authorName->setString("Max Mainio Beidler");
		info->customOPInfo.authorEmail->setString("beidler.max@gmail.com");

		info->customOPInfo.minInputs = 1;
		info->customOPInfo.maxInputs = 1;
	}

	DLLEXPORT
	TD::TOP_CPlusPlusBase*
	CreateTOPInstance(const TD::OP_NodeInfo* info, TD::TOP_Context* context)
	{
		return new AppleSubjectMask(info, context);
	}

	DLLEXPORT
	void
	DestroyTOPInstance(TD::TOP_CPlusPlusBase* instance, TD::TOP_Context* context)
	{
		delete (AppleSubjectMask*)instance;
	}
};

AppleSubjectMask::AppleSubjectMask(const TD::OP_NodeInfo* info, TD::TOP_Context* context)
	: myContext(context),
	  myExecuteCount(0)
{
	currentQualityLevel = 1;

	segmentationRequest = [[VNGeneratePersonSegmentationRequest alloc] init];
	segmentationRequest.qualityLevel =
		VNGeneratePersonSegmentationRequestQualityLevelBalanced;
	segmentationRequest.outputPixelFormat =
		kCVPixelFormatType_OneComponent8;
}

AppleSubjectMask::~AppleSubjectMask()
{
	if (inputPool)
	{
		CVPixelBufferPoolRelease(inputPool);
	}
}

void
AppleSubjectMask::getGeneralInfo(
	TD::TOP_GeneralInfo* ginfo,
	const TD::OP_Inputs* inputs,
	void* reserved1)
{
	ginfo->cookEveryFrame = false;
	ginfo->cookEveryFrameIfAsked = false;
}

void
AppleSubjectMask::ensurePool(size_t width, size_t height)
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
AppleSubjectMask::runSegmentation(
	CVPixelBufferRef inputBuffer,
	int qualityLevel)
{
	@autoreleasepool
	{
		VNGeneratePersonSegmentationRequest* request =
			[[VNGeneratePersonSegmentationRequest alloc] init];

		request.outputPixelFormat =
			kCVPixelFormatType_OneComponent8;

		switch (qualityLevel)
		{
			case 0:
				request.qualityLevel =
					VNGeneratePersonSegmentationRequestQualityLevelAccurate;
				break;

			case 1:
				request.qualityLevel =
					VNGeneratePersonSegmentationRequestQualityLevelBalanced;
				break;

			case 2:
				request.qualityLevel =
					VNGeneratePersonSegmentationRequestQualityLevelFast;
				break;

			default:
				request.qualityLevel =
					VNGeneratePersonSegmentationRequestQualityLevelBalanced;
				break;
		}

		VNImageRequestHandler* handler =
			[[VNImageRequestHandler alloc]
				initWithCVPixelBuffer:inputBuffer
				options:@{}];

		NSError* error = nil;

		[handler performRequests:@[request] error:&error];

		if (!error && request.results.count > 0)
		{
			VNPixelBufferObservation* observation =
				request.results.firstObject;

			CVPixelBufferRef maskBuf =
				observation.pixelBuffer;

			if (maskBuf)
			{
				CVPixelBufferLockBaseAddress(
					maskBuf,
					kCVPixelBufferLock_ReadOnly
				);

				size_t mw = CVPixelBufferGetWidth(maskBuf);
				size_t mh = CVPixelBufferGetHeight(maskBuf);

				uint8_t* maskData =
					(uint8_t*)CVPixelBufferGetBaseAddress(maskBuf);

				size_t maskBPR =
					CVPixelBufferGetBytesPerRow(maskBuf);

				{
					std::lock_guard<std::mutex> lock(maskMutex);

					cachedMaskW = mw;
					cachedMaskH = mh;
					cachedMaskData.resize(mw * mh);

					for (size_t y = 0; y < mh; ++y)
					{
						memcpy(
							cachedMaskData.data() + y * mw,
							maskData + y * maskBPR,
							mw
						);
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
AppleSubjectMask::setupParameters(
	TD::OP_ParameterManager* manager,
	void* reserved1)
{
	{
		TD::OP_StringParameter sp;

		sp.name = "Quality";
		sp.label = "Vision Quality";
		sp.defaultValue = "Balanced";
		sp.page = "Subject Mask";

		const char* names[] =
		{
			"Accurate",
			"Balanced",
			"Fast"
		};

		const char* labels[] =
		{
			"Accurate",
			"Balanced",
			"Fast"
		};

		manager->appendMenu(sp, 3, names, labels);
	}

	{
		TD::OP_StringParameter sp;

		sp.name = "Resolution";
		sp.label = "Output Resolution";
		sp.defaultValue = "Matchinput";
		sp.page = "Subject Mask";

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
}

void
AppleSubjectMask::pulsePressed(
	const char* name,
	void* reserved1)
{
}

void
AppleSubjectMask::execute(
	TD::TOP_Output* output,
	const TD::OP_Inputs* inputs,
	void* reserved1)
{
	myExecuteCount++;

	int qualityIndex = inputs->getParInt("Quality");
	int resolutionMode = inputs->getParInt("Resolution");

	if (qualityIndex != currentQualityLevel)
	{
		currentQualityLevel = qualityIndex;
		hasCachedMask.store(false);
	}

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

		runSegmentation(
			poolBuffer,
			currentQualityLevel
		);
	}

	if (!hasCachedMask.load())
	{
		return;
	}

	std::lock_guard<std::mutex> lock(maskMutex);

	size_t maskW = cachedMaskW;
	size_t maskH = cachedMaskH;

	uint8_t* maskData =
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
		outW * outH;

	TD::OP_SmartRef<TD::TOP_Buffer> outBuffer =
		myContext->createOutputBuffer(
			outSize,
			TD::TOP_BufferFlags::None,
			nullptr
		);

	uint8_t* outData =
		(uint8_t*)outBuffer->data;

	if (sameSize)
	{
		for (size_t y = 0; y < outH; ++y)
		{
			uint8_t* maskRow =
				maskData + (y * maskW);

			uint8_t* outRow =
				outData + (y * outW);

			memcpy(outRow, maskRow, outW);
		}
	}
	else
	{
		for (size_t y = 0; y < outH; ++y)
		{
			uint8_t* outRow =
				outData + (y * outW);

			uint32_t fpY = y * fpScaleY;

			size_t y0 = fpY >> 16;
			size_t y1 =
				(y0 < maskH - 1) ? y0 + 1 : y0;

			uint32_t wy =
				(fpY & 0xFFFF) >> 8;

			uint8_t* row0 =
				maskData + (y0 * maskW);

			uint8_t* row1 =
				maskData + (y1 * maskW);

			uint32_t currentFpX = 0;

			for (size_t x = 0; x < outW; ++x)
			{
				size_t x0 = currentFpX >> 16;
				size_t x1 =
					(x0 < maskW - 1) ? x0 + 1 : x0;

				uint32_t wx =
					(currentFpX & 0xFFFF) >> 8;

				uint32_t v00 = row0[x0];
				uint32_t v01 = row0[x1];
				uint32_t v10 = row1[x0];
				uint32_t v11 = row1[x1];

				uint32_t top =
					(v00 * (256 - wx) + v01 * wx) >> 8;

				uint32_t bot =
					(v10 * (256 - wx) + v11 * wx) >> 8;

				uint32_t val =
					(top * (256 - wy) + bot * wy) >> 8;

				outRow[x] = (uint8_t)val;

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
		TD::OP_PixelFormat::Mono8Fixed;

	uploadInfo.firstPixel =
		TD::TOP_FirstPixel::TopLeft;

	output->uploadBuffer(
		&outBuffer,
		uploadInfo,
		nullptr
	);
}