#include "AppleSaliencyMap.h"
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

		info->customOPInfo.opType->setString("Applesaliencymap");
		info->customOPInfo.opLabel->setString("Apple Saliency Map");
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
		return new AppleSaliencyMap(info, context);
	}

	DLLEXPORT
	void
	DestroyTOPInstance(TD::TOP_CPlusPlusBase* instance, TD::TOP_Context* context)
	{
		delete (AppleSaliencyMap*)instance;
	}
};

AppleSaliencyMap::AppleSaliencyMap(const TD::OP_NodeInfo* info, TD::TOP_Context* context)
	: myContext(context),
	  myExecuteCount(0)
{
	currentMode = 0;
}

AppleSaliencyMap::~AppleSaliencyMap()
{
	if (inputPool)
	{
		CVPixelBufferPoolRelease(inputPool);
	}
}

void
AppleSaliencyMap::getGeneralInfo(
	TD::TOP_GeneralInfo* ginfo,
	const TD::OP_Inputs* inputs,
	void* reserved1)
{
	ginfo->cookEveryFrame = false;
	ginfo->cookEveryFrameIfAsked = false;
}

void
AppleSaliencyMap::ensurePool(size_t width, size_t height)
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
AppleSaliencyMap::runSaliency(
	CVPixelBufferRef inputBuffer,
	int mode)
{
	@autoreleasepool
	{
		VNImageBasedRequest* request = nil;

		if (mode == 0)
		{
			request = [[VNGenerateAttentionBasedSaliencyImageRequest alloc] init];
		}
		else
		{
			request = [[VNGenerateObjectnessBasedSaliencyImageRequest alloc] init];
		}

		VNImageRequestHandler* handler =
			[[VNImageRequestHandler alloc]
				initWithCVPixelBuffer:inputBuffer
				options:@{}];

		NSError* error = nil;

		[handler performRequests:@[request] error:&error];

		if (!error && request.results.count > 0)
		{
			VNSaliencyImageObservation* observation =
				(VNSaliencyImageObservation*)request.results.firstObject;

			CVPixelBufferRef heatmapBuf =
				observation.pixelBuffer;

			if (heatmapBuf)
			{
				CVPixelBufferLockBaseAddress(
					heatmapBuf,
					kCVPixelBufferLock_ReadOnly
				);

				size_t hw = CVPixelBufferGetWidth(heatmapBuf);
				size_t hh = CVPixelBufferGetHeight(heatmapBuf);

				float* heatmapData =
					(float*)CVPixelBufferGetBaseAddress(heatmapBuf);

				size_t heatmapBPR =
					CVPixelBufferGetBytesPerRow(heatmapBuf);

				{
					std::lock_guard<std::mutex> lock(heatmapMutex);

					cachedHeatmapW = hw;
					cachedHeatmapH = hh;

					cachedHeatmapData.resize(hw * hh);

					for (size_t y = 0; y < hh; ++y)
					{
						float* srcRow =
							(float*)((uint8_t*)heatmapData + y * heatmapBPR);

						memcpy(
							cachedHeatmapData.data() + y * hw,
							srcRow,
							hw * sizeof(float)
						);
					}
				}

				CVPixelBufferUnlockBaseAddress(
					heatmapBuf,
					kCVPixelBufferLock_ReadOnly
				);

				hasCachedHeatmap.store(true);
			}
		}

		CVPixelBufferRelease(inputBuffer);
	}
}

void
AppleSaliencyMap::setupParameters(
	TD::OP_ParameterManager* manager,
	void* reserved1)
{
	// Saliency parameter.
	{
		TD::OP_StringParameter sp;

		sp.name = "Mode";
		sp.label = "Saliency Mode";
		sp.defaultValue = "Attention";
		sp.page = "Saliency Map";

		const char* names[] =
		{
			"Attention",
			"Objectness"
		};

		const char* labels[] =
		{
			"Attention",
			"Objectness"
		};

		manager->appendMenu(sp, 2, names, labels);
	}

	// Output Resolution
	{
		TD::OP_StringParameter sp;

		sp.name = "Resolution";
		sp.label = "Output Resolution";
		sp.defaultValue = "Matchinput";
		sp.page = "Saliency Map";

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
AppleSaliencyMap::pulsePressed(
	const char* name,
	void* reserved1)
{
}

void
AppleSaliencyMap::execute(
	TD::TOP_Output* output,
	const TD::OP_Inputs* inputs,
	void* reserved1)
{
	myExecuteCount++;

	int mode = inputs->getParInt("Mode");
	int resolutionMode = inputs->getParInt("Resolution");

	if (mode != currentMode)
	{
		currentMode = mode;
		hasCachedHeatmap.store(false);
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

	downloadOpts.pixelFormat =
		TD::OP_PixelFormat::BGRA8Fixed;

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

	{
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

			hasCachedHeatmap.store(false);

			runSaliency(
				poolBuffer,
				currentMode
			);
		}
	}

	if (!hasCachedHeatmap.load())
	{
		return;
	}

	std::lock_guard<std::mutex> lock(heatmapMutex);

	size_t heatmapW = cachedHeatmapW;
	size_t heatmapH = cachedHeatmapH;

	float* heatmapData =
		cachedHeatmapData.data();

	TD::OP_TextureDesc suggestedDesc;
	output->getSuggestedOutputDesc(&suggestedDesc, nullptr);

	size_t commonW =
		(suggestedDesc.width > 0) ? suggestedDesc.width : width;

	size_t commonH =
		(suggestedDesc.height > 0) ? suggestedDesc.height : height;

	// Match Input follows TouchDesigner's Common page resolution.
	// Vision Native outputs the raw Vision heatmap resolution.
	size_t outW =
		(resolutionMode == 1) ? heatmapW : commonW;

	size_t outH =
		(resolutionMode == 1) ? heatmapH : commonH;

	bool sameSize =
		(heatmapW == outW && heatmapH == outH);

	float scaleX = 0.0f;
	float scaleY = 0.0f;

	if (!sameSize)
	{
		scaleX =
			(float)heatmapW / (float)outW;

		scaleY =
			(float)heatmapH / (float)outH;
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
			float* srcRow =
				heatmapData + (y * heatmapW);

			float* outRow =
				outData + (y * outW);

			memcpy(
				outRow,
				srcRow,
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

			float srcYf = (float)y * scaleY;
			size_t y0 = (size_t)srcYf;
			size_t y1 = (y0 < heatmapH - 1) ? y0 + 1 : y0;
			float wy = srcYf - (float)y0;

			float* row0 =
				heatmapData + (y0 * heatmapW);

			float* row1 =
				heatmapData + (y1 * heatmapW);

			for (size_t x = 0; x < outW; ++x)
			{
				float srcXf = (float)x * scaleX;
				size_t x0 = (size_t)srcXf;
				size_t x1 = (x0 < heatmapW - 1) ? x0 + 1 : x0;
				float wx = srcXf - (float)x0;

				float v00 = row0[x0];
				float v01 = row0[x1];
				float v10 = row1[x0];
				float v11 = row1[x1];

				float top =
					v00 * (1.0f - wx) + v01 * wx;

				float bot =
					v10 * (1.0f - wx) + v11 * wx;

				float val =
					top * (1.0f - wy) + bot * wy;

				outRow[x] = val;
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