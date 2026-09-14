#include "AppleOpticalFlow.h"

#include <iostream>
#include <algorithm>
#include <cmath>

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

		info->customOPInfo.opType->setString("Appleopticalflow");
		info->customOPInfo.opLabel->setString("Apple Optical Flow");
		info->customOPInfo.opIcon->setString("AOF");

		info->customOPInfo.authorName->setString("Max Mainio Beidler");
		info->customOPInfo.authorEmail->setString("beidler.max@gmail.com");

		info->customOPInfo.minInputs = 1;
		info->customOPInfo.maxInputs = 1;
	}

	DLLEXPORT
	TD::TOP_CPlusPlusBase*
	CreateTOPInstance(const TD::OP_NodeInfo* info, TD::TOP_Context* context)
	{
		return new AppleOpticalFlow(info, context);
	}

	DLLEXPORT
	void
	DestroyTOPInstance(TD::TOP_CPlusPlusBase* instance, TD::TOP_Context* context)
	{
		delete (AppleOpticalFlow*)instance;
	}
};

AppleOpticalFlow::AppleOpticalFlow(const TD::OP_NodeInfo* info, TD::TOP_Context* context)
	: myContext(context),
	  myExecuteCount(0)
{
}

AppleOpticalFlow::~AppleOpticalFlow()
{
	clearHistory();

	if (inputPool)
	{
		CVPixelBufferPoolRelease(inputPool);
	}
}

void
AppleOpticalFlow::getGeneralInfo(
	TD::TOP_GeneralInfo* ginfo,
	const TD::OP_Inputs* inputs,
	void* reserved1)
{
	ginfo->cookEveryFrame = false;
	ginfo->cookEveryFrameIfAsked = false;
}

void
AppleOpticalFlow::ensureInputPool(size_t width, size_t height)
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
AppleOpticalFlow::clearHistory()
{
	if (previousInputBuffer)
	{
		CVPixelBufferRelease(previousInputBuffer);
		previousInputBuffer = nullptr;
	}

	previousInputW = 0;
	previousInputH = 0;

	hasCachedFlow.store(false);

	std::lock_guard<std::mutex> lock(flowMutex);

	cachedFlowData.clear();
	cachedFlowW = 0;
	cachedFlowH = 0;
}

float
AppleOpticalFlow::halfToFloat(uint16_t h)
{
	uint16_t hExp =
		h & 0x7C00u;

	uint16_t hSig =
		h & 0x03FFu;

	uint32_t fSign =
		((uint32_t)h & 0x8000u) << 16;

	uint32_t fExp;
	uint32_t fSig;

	if (hExp == 0)
	{
		if (hSig == 0)
		{
			uint32_t f = fSign;
			float out;
			std::memcpy(&out, &f, sizeof(float));
			return out;
		}

		int shift = 0;

		while ((hSig & 0x0400u) == 0)
		{
			hSig <<= 1;
			shift++;
		}

		hSig &= 0x03FFu;
		fExp =
			(uint32_t)(127 - 15 - shift) << 23;

		fSig =
			(uint32_t)hSig << 13;
	}
	else if (hExp == 0x7C00u)
	{
		fExp =
			0xFFu << 23;

		fSig =
			(uint32_t)hSig << 13;
	}
	else
	{
		fExp =
			(uint32_t)(((hExp >> 10) - 15 + 127) & 0xFF) << 23;

		fSig =
			(uint32_t)hSig << 13;
	}

	uint32_t f =
		fSign | fExp | fSig;

	float out;
	std::memcpy(&out, &f, sizeof(float));
	return out;
}

bool
AppleOpticalFlow::cacheFlowFromPixelBuffer(
	CVPixelBufferRef flowBuffer,
	bool normalize,
	float normalizeScale)
{
	if (!flowBuffer)
	{
		return false;
	}

	CVPixelBufferLockBaseAddress(
		flowBuffer,
		kCVPixelBufferLock_ReadOnly
	);

	size_t flowW =
		CVPixelBufferGetWidth(flowBuffer);

	size_t flowH =
		CVPixelBufferGetHeight(flowBuffer);

	size_t flowBPR =
		CVPixelBufferGetBytesPerRow(flowBuffer);

	void* baseAddress =
		CVPixelBufferGetBaseAddress(flowBuffer);

	OSType pixelFormat =
		CVPixelBufferGetPixelFormatType(flowBuffer);

	if (!baseAddress || flowW == 0 || flowH == 0)
	{
		CVPixelBufferUnlockBaseAddress(
			flowBuffer,
			kCVPixelBufferLock_ReadOnly
		);

		return false;
	}

	std::vector<float> temp;
	temp.resize(flowW * flowH * 2);

	for (size_t y = 0; y < flowH; ++y)
	{
		uint8_t* rowBytes =
			(uint8_t*)baseAddress + y * flowBPR;

		for (size_t x = 0; x < flowW; ++x)
		{
			float vx = 0.0f;
			float vy = 0.0f;

			if (pixelFormat == kCVPixelFormatType_TwoComponent32Float)
			{
				float* row =
					(float*)rowBytes;

				vx =
					row[x * 2 + 0];

				vy =
					row[x * 2 + 1];
			}
			else if (pixelFormat == kCVPixelFormatType_TwoComponent16Half)
			{
				uint16_t* row =
					(uint16_t*)rowBytes;

				vx =
					halfToFloat(row[x * 2 + 0]);

				vy =
					halfToFloat(row[x * 2 + 1]);
			}
			else
			{
				CVPixelBufferUnlockBaseAddress(
					flowBuffer,
					kCVPixelBufferLock_ReadOnly
				);

				std::cout
					<< "Apple Optical Flow: Unsupported flow pixel format: "
					<< pixelFormat
					<< std::endl;

				return false;
			}

			if (!std::isfinite(vx))
			{
				vx = 0.0f;
			}

			if (!std::isfinite(vy))
			{
				vy = 0.0f;
			}

			if (normalize)
			{
				vx =
					vx / normalizeScale;

				vy =
					vy / normalizeScale;
			}

			temp[(y * flowW + x) * 2 + 0] =
				vx;

			temp[(y * flowW + x) * 2 + 1] =
				vy;
		}
	}

	CVPixelBufferUnlockBaseAddress(
		flowBuffer,
		kCVPixelBufferLock_ReadOnly
	);

	{
		std::lock_guard<std::mutex> lock(flowMutex);

		cachedFlowW =
			flowW;

		cachedFlowH =
			flowH;

		cachedFlowData.swap(temp);
	}

	hasCachedFlow.store(true);

	return true;
}

bool
AppleOpticalFlow::runOpticalFlow(
	CVPixelBufferRef previousBuffer,
	CVPixelBufferRef currentBuffer,
	int accuracyIndex,
	bool normalize,
	float normalizeScale)
{
	@autoreleasepool
	{
		hasCachedFlow.store(false);

		if (!previousBuffer || !currentBuffer)
		{
			return false;
		}

		VNGenerateOpticalFlowRequest* request =
			[[VNGenerateOpticalFlowRequest alloc]
				initWithTargetedCVPixelBuffer:previousBuffer
				options:@{}];

		if (!request)
		{
			std::cout << "Apple Optical Flow: Could not create request." << std::endl;
			return false;
		}

		switch (accuracyIndex)
		{
			case 0:
				request.computationAccuracy =
					VNGenerateOpticalFlowRequestComputationAccuracyLow;
				break;

			case 1:
				request.computationAccuracy =
					VNGenerateOpticalFlowRequestComputationAccuracyMedium;
				break;

			case 2:
			default:
				request.computationAccuracy =
					VNGenerateOpticalFlowRequestComputationAccuracyHigh;
				break;
		}

		request.outputPixelFormat =
			kCVPixelFormatType_TwoComponent32Float;

		NSError* error = nil;

		VNImageRequestHandler* handler =
			[[VNImageRequestHandler alloc]
				initWithCVPixelBuffer:currentBuffer
				options:@{}];

		[handler performRequests:@[request] error:&error];

		if (error)
		{
			std::cout
				<< "Apple Optical Flow: VNGenerateOpticalFlowRequest failed: "
				<< [[error localizedDescription] UTF8String]
				<< std::endl;

			return false;
		}

		if (request.results.count <= 0)
		{
			std::cout << "Apple Optical Flow: No results." << std::endl;
			return false;
		}

		VNPixelBufferObservation* observation =
			(VNPixelBufferObservation*)request.results.firstObject;

		if (!observation ||
			![observation isKindOfClass:[VNPixelBufferObservation class]])
		{
			std::cout << "Apple Optical Flow: Result was not a pixel buffer observation." << std::endl;
			return false;
		}

		return cacheFlowFromPixelBuffer(
			observation.pixelBuffer,
			normalize,
			normalizeScale
		);
	}
}

void
AppleOpticalFlow::setupParameters(
	TD::OP_ParameterManager* manager,
	void* reserved1)
{
	{
		TD::OP_StringParameter sp;

		sp.name = "Accuracy";
		sp.label = "Accuracy";
		sp.defaultValue = "High";
		sp.page = "Optical Flow";

		const char* names[] =
		{
			"Low",
			"Medium",
			"High"
		};

		const char* labels[] =
		{
			"Low",
			"Medium",
			"High"
		};

		manager->appendMenu(sp, 3, names, labels);
	}

	{
		TD::OP_NumericParameter np;

		np.name = "Normalize";
		np.label = "Normalize";
		np.page = "Optical Flow";
		np.defaultValues[0] = 1.0;

		manager->appendToggle(np);
	}

	{
		TD::OP_NumericParameter np;

		np.name = "Normalizescale";
		np.label = "Normalize Scale";
		np.page = "Optical Flow";
		np.defaultValues[0] = 64.0;
		np.minSliders[0] = 1.0;
		np.maxSliders[0] = 512.0;
		np.minValues[0] = 0.0001;
		np.maxValues[0] = 100000.0;

		manager->appendFloat(np);
	}

	{
		TD::OP_NumericParameter np;

		np.name = "Resethistory";
		np.label = "Reset History";
		np.page = "Optical Flow";

		manager->appendPulse(np);
	}
}

void
AppleOpticalFlow::pulsePressed(
	const char* name,
	void* reserved1)
{
	if (std::strcmp(name, "Resethistory") == 0)
	{
		clearHistory();
		std::cout << "Apple Optical Flow history reset." << std::endl;
		return;
	}
}

void
AppleOpticalFlow::execute(
	TD::TOP_Output* output,
	const TD::OP_Inputs* inputs,
	void* reserved1)
{
	myExecuteCount++;

	int accuracyIndex =
		inputs->getParInt("Accuracy");

	int normalize =
		inputs->getParInt("Normalize");

	double normalizeScaleValue =
		inputs->getParDouble("Normalizescale");

	float normalizeScale =
		(float)normalizeScaleValue;

	if (normalizeScale <= 0.000001f)
	{
		normalizeScale =
			1.0f;
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

	downloadOpts.verticalFlip =
		true;

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

	ensureInputPool(width, height);

	CVPixelBufferRef currentBuffer = nullptr;

	CVReturn cvResult =
		CVPixelBufferPoolCreatePixelBuffer(
			nullptr,
			inputPool,
			&currentBuffer
		);

	if (cvResult != kCVReturnSuccess || !currentBuffer)
	{
		return;
	}

	CVPixelBufferLockBaseAddress(currentBuffer, 0);

	uint8_t* destData =
		(uint8_t*)CVPixelBufferGetBaseAddress(currentBuffer);

	size_t destBPR =
		CVPixelBufferGetBytesPerRow(currentBuffer);

	uint8_t* srcData =
		(uint8_t*)downloadResult->getData();

	size_t srcBPR =
		width * 4;

	for (size_t y = 0; y < height; ++y)
	{
		std::memcpy(
			destData + y * destBPR,
			srcData + y * srcBPR,
			srcBPR
		);
	}

	CVPixelBufferUnlockBaseAddress(currentBuffer, 0);

	bool hasPrevious =
		previousInputBuffer &&
		previousInputW == width &&
		previousInputH == height;

	if (!hasPrevious)
	{
		clearHistory();

		previousInputBuffer =
			currentBuffer;

		CVPixelBufferRetain(previousInputBuffer);

		previousInputW =
			width;

		previousInputH =
			height;

		CVPixelBufferRelease(currentBuffer);
		return;
	}

	bool success =
		runOpticalFlow(
			previousInputBuffer,
			currentBuffer,
			accuracyIndex,
			normalize != 0,
			normalizeScale
		);

	CVPixelBufferRelease(previousInputBuffer);

	previousInputBuffer =
		currentBuffer;

	CVPixelBufferRetain(previousInputBuffer);

	previousInputW =
		width;

	previousInputH =
		height;

	CVPixelBufferRelease(currentBuffer);

	if (!success || !hasCachedFlow.load())
	{
		return;
	}

	std::lock_guard<std::mutex> lock(flowMutex);

	size_t flowW =
		cachedFlowW;

	size_t flowH =
		cachedFlowH;

	float* flowData =
		cachedFlowData.data();

	uint64_t outSize =
		flowW * flowH * 2 * sizeof(float);

	TD::OP_SmartRef<TD::TOP_Buffer> outBuffer =
		myContext->createOutputBuffer(
			outSize,
			TD::TOP_BufferFlags::None,
			nullptr
		);

	float* outData =
		(float*)outBuffer->data;

	std::memcpy(
		outData,
		flowData,
		outSize
	);

	TD::TOP_UploadInfo uploadInfo;

	uploadInfo.textureDesc.width =
		(uint32_t)flowW;

	uploadInfo.textureDesc.height =
		(uint32_t)flowH;

	uploadInfo.textureDesc.texDim =
		TD::OP_TexDim::e2D;

	uploadInfo.textureDesc.pixelFormat =
		TD::OP_PixelFormat::RG32Float;

	uploadInfo.firstPixel =
		TD::TOP_FirstPixel::TopLeft;

	output->uploadBuffer(
		&outBuffer,
		uploadInfo,
		nullptr
	);
}