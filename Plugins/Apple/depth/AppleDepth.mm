#include "AppleDepth.h"

#include <iostream>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

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

		info->customOPInfo.opType->setString("Appledepth");
		info->customOPInfo.opLabel->setString("Apple Depth");
		info->customOPInfo.opIcon->setString("ADP");

		info->customOPInfo.authorName->setString("Max Mainio Beidler");
		info->customOPInfo.authorEmail->setString("beidler.max@gmail.com");

		info->customOPInfo.minInputs = 1;
		info->customOPInfo.maxInputs = 1;
	}

	DLLEXPORT
	TD::TOP_CPlusPlusBase*
	CreateTOPInstance(const TD::OP_NodeInfo* info, TD::TOP_Context* context)
	{
		return new AppleDepth(info, context);
	}

	DLLEXPORT
	void
	DestroyTOPInstance(TD::TOP_CPlusPlusBase* instance, TD::TOP_Context* context)
	{
		delete (AppleDepth*)instance;
	}
};

AppleDepth::AppleDepth(const TD::OP_NodeInfo* info, TD::TOP_Context* context)
	: myContext(context),
	  myExecuteCount(0)
{
}

AppleDepth::~AppleDepth()
{
	unloadModel();

	if (inputPool)
	{
		CVPixelBufferPoolRelease(inputPool);
	}
}

void
AppleDepth::getGeneralInfo(
	TD::TOP_GeneralInfo* ginfo,
	const TD::OP_Inputs* inputs,
	void* reserved1)
{
	ginfo->cookEveryFrame = false;
	ginfo->cookEveryFrameIfAsked = false;
}

void
AppleDepth::ensurePool(size_t width, size_t height)
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
AppleDepth::unloadModel()
{
	depthModel = nil;
	visionDepthModel = nil;
	currentModelPath.clear();
	modelLoaded = false;
}

bool
AppleDepth::ensureModelLoaded(const char* modelPath)
{
	if (!modelPath || std::strlen(modelPath) == 0)
	{
		std::cout << "Apple Depth: No model file selected." << std::endl;
		return false;
	}

	if (modelLoaded && currentModelPath == modelPath && visionDepthModel)
	{
		return true;
	}

	unloadModel();

	@autoreleasepool
	{
		NSString* pathString =
			[NSString stringWithUTF8String:modelPath];

		if (!pathString || pathString.length == 0)
		{
			std::cout << "Apple Depth: Invalid model path." << std::endl;
			return false;
		}

		BOOL isDirectory = NO;

		if (![[NSFileManager defaultManager]
				fileExistsAtPath:pathString
				isDirectory:&isDirectory])
		{
			std::cout
				<< "Apple Depth: Model file does not exist: "
				<< modelPath
				<< std::endl;

			return false;
		}

		NSURL* modelURL =
			[NSURL fileURLWithPath:pathString];

		NSError* error = nil;

		NSURL* compiledURL =
			[MLModel compileModelAtURL:modelURL error:&error];

		if (!compiledURL)
		{
			if (error)
			{
				std::cout
					<< "Apple Depth: Model compile failed, trying direct load: "
					<< [[error localizedDescription] UTF8String]
					<< std::endl;
			}

			error = nil;
			depthModel =
				[MLModel modelWithContentsOfURL:modelURL
										  error:&error];
		}
		else
		{
			MLModelConfiguration* config =
				[[MLModelConfiguration alloc] init];

			depthModel =
				[MLModel modelWithContentsOfURL:compiledURL
								  configuration:config
										  error:&error];
		}

		if (!depthModel)
		{
			if (error)
			{
				std::cout
					<< "Apple Depth: Could not load model: "
					<< [[error localizedDescription] UTF8String]
					<< std::endl;
			}
			else
			{
				std::cout << "Apple Depth: Could not load model." << std::endl;
			}

			return false;
		}

		error = nil;

		visionDepthModel =
			[VNCoreMLModel modelForMLModel:depthModel error:&error];

		if (!visionDepthModel)
		{
			if (error)
			{
				std::cout
					<< "Apple Depth: Could not create VNCoreMLModel: "
					<< [[error localizedDescription] UTF8String]
					<< std::endl;
			}

			unloadModel();
			return false;
		}

		currentModelPath = modelPath;
		modelLoaded = true;

		std::cout
			<< "Apple Depth: Loaded model: "
			<< modelPath
			<< std::endl;

		return true;
	}
}

float
AppleDepth::halfToFloat(uint16_t h)
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
		fExp = (uint32_t)(127 - 15 - shift) << 23;
		fSig = (uint32_t)hSig << 13;
	}
	else if (hExp == 0x7C00u)
	{
		fExp = 0xFFu << 23;
		fSig = (uint32_t)hSig << 13;
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
AppleDepth::cacheDepthFromMultiArray(
	MLMultiArray* array,
	bool normalize,
	bool invert)
{
	if (!array)
	{
		return false;
	}

	NSArray<NSNumber*>* shape =
		array.shape;

	NSArray<NSNumber*>* strides =
		array.strides;

	if (shape.count < 2 || strides.count < 2)
	{
		std::cout << "Apple Depth: Unexpected depth output shape." << std::endl;
		return false;
	}

	// Use the last two dimensions as height and width.
	// This supports common shapes such as HxW, 1xHxW, or 1x1xHxW.
	NSInteger hIndex =
		(NSInteger)shape.count - 2;

	NSInteger wIndex =
		(NSInteger)shape.count - 1;

	size_t depthH =
		(size_t)[shape[hIndex] integerValue];

	size_t depthW =
		(size_t)[shape[wIndex] integerValue];

	NSInteger strideH =
		[strides[hIndex] integerValue];

	NSInteger strideW =
		[strides[wIndex] integerValue];

	if (depthW == 0 || depthH == 0)
	{
		std::cout << "Apple Depth: Depth output has empty dimensions." << std::endl;
		return false;
	}

	std::vector<float> temp;
	temp.resize(depthW * depthH);

	float minValue =
		std::numeric_limits<float>::max();

	float maxValue =
		std::numeric_limits<float>::lowest();

	void* dataPointer =
		array.dataPointer;

	for (size_t y = 0; y < depthH; ++y)
	{
		for (size_t x = 0; x < depthW; ++x)
		{
			NSInteger offset =
				(NSInteger)y * strideH +
				(NSInteger)x * strideW;

			float v = 0.0f;

			if (array.dataType == MLMultiArrayDataTypeFloat32)
			{
				v =
					((float*)dataPointer)[offset];
			}
			else if (array.dataType == MLMultiArrayDataTypeDouble)
			{
				v =
					(float)((double*)dataPointer)[offset];
			}
			else if (array.dataType == MLMultiArrayDataTypeFloat16)
			{
				v =
					halfToFloat(((uint16_t*)dataPointer)[offset]);
			}
			else
			{
				std::cout << "Apple Depth: Unsupported MLMultiArray data type." << std::endl;
				return false;
			}

			if (!std::isfinite(v))
			{
				v = 0.0f;
			}

			temp[y * depthW + x] = v;

			minValue = std::min(minValue, v);
			maxValue = std::max(maxValue, v);
		}
	}

	float range =
		maxValue - minValue;

	for (size_t i = 0; i < temp.size(); ++i)
	{
		float v =
			temp[i];

		if (normalize)
		{
			if (range > 0.000001f)
			{
				v =
					(v - minValue) / range;
			}
			else
			{
				v = 0.0f;
			}

			if (invert)
			{
				v =
					1.0f - v;
			}
		}
		else if (invert)
		{
			v =
				(minValue + maxValue) - v;
		}

		temp[i] = v;
	}

	{
		std::lock_guard<std::mutex> lock(depthMutex);

		cachedDepthW = depthW;
		cachedDepthH = depthH;
		cachedDepthData.swap(temp);
	}

	hasCachedDepth.store(true);

	return true;
}

bool
AppleDepth::cacheDepthFromPixelBuffer(
	CVPixelBufferRef pixelBuffer,
	bool normalize,
	bool invert)
{
	if (!pixelBuffer)
	{
		return false;
	}

	CVPixelBufferLockBaseAddress(
		pixelBuffer,
		kCVPixelBufferLock_ReadOnly
	);

	size_t depthW =
		CVPixelBufferGetWidth(pixelBuffer);

	size_t depthH =
		CVPixelBufferGetHeight(pixelBuffer);

	size_t depthBPR =
		CVPixelBufferGetBytesPerRow(pixelBuffer);

	void* baseAddress =
		CVPixelBufferGetBaseAddress(pixelBuffer);

	OSType pixelFormat =
		CVPixelBufferGetPixelFormatType(pixelBuffer);

	if (!baseAddress || depthW == 0 || depthH == 0)
	{
		CVPixelBufferUnlockBaseAddress(
			pixelBuffer,
			kCVPixelBufferLock_ReadOnly
		);

		return false;
	}

	std::vector<float> temp;
	temp.resize(depthW * depthH);

	float minValue =
		std::numeric_limits<float>::max();

	float maxValue =
		std::numeric_limits<float>::lowest();

	for (size_t y = 0; y < depthH; ++y)
	{
		uint8_t* rowBytes =
			(uint8_t*)baseAddress + y * depthBPR;

		for (size_t x = 0; x < depthW; ++x)
		{
			float v = 0.0f;

			if (pixelFormat == kCVPixelFormatType_OneComponent32Float)
			{
				float* row =
					(float*)rowBytes;

				v =
					row[x];
			}
			else if (pixelFormat == kCVPixelFormatType_OneComponent16Half)
			{
				uint16_t* row =
					(uint16_t*)rowBytes;

				v =
					halfToFloat(row[x]);
			}
			else if (pixelFormat == kCVPixelFormatType_OneComponent8)
			{
				uint8_t* row =
					(uint8_t*)rowBytes;

				v =
					(float)row[x] / 255.0f;
			}
			else if (pixelFormat == kCVPixelFormatType_32BGRA)
			{
				uint8_t* row =
					(uint8_t*)rowBytes;

				// BGRA layout. Use the red channel as a grayscale fallback.
				v =
					(float)row[x * 4 + 2] / 255.0f;
			}
			else
			{
				CVPixelBufferUnlockBaseAddress(
					pixelBuffer,
					kCVPixelBufferLock_ReadOnly
				);

				std::cout
					<< "Apple Depth: Unsupported CVPixelBuffer pixel format: "
					<< pixelFormat
					<< std::endl;

				return false;
			}

			if (!std::isfinite(v))
			{
				v = 0.0f;
			}

			temp[y * depthW + x] = v;

			minValue =
				std::min(minValue, v);

			maxValue =
				std::max(maxValue, v);
		}
	}

	CVPixelBufferUnlockBaseAddress(
		pixelBuffer,
		kCVPixelBufferLock_ReadOnly
	);

	float range =
		maxValue - minValue;

	for (size_t i = 0; i < temp.size(); ++i)
	{
		float v =
			temp[i];

		if (normalize)
		{
			if (range > 0.000001f)
			{
				v =
					(v - minValue) / range;
			}
			else
			{
				v = 0.0f;
			}

			if (invert)
			{
				v =
					1.0f - v;
			}
		}
		else if (invert)
		{
			v =
				(minValue + maxValue) - v;
		}

		temp[i] = v;
	}

	{
		std::lock_guard<std::mutex> lock(depthMutex);

		cachedDepthW =
			depthW;

		cachedDepthH =
			depthH;

		cachedDepthData.swap(temp);
	}

	hasCachedDepth.store(true);

	return true;
}

void
AppleDepth::runDepth(
	CVPixelBufferRef inputBuffer,
	const char* modelPath,
	bool normalize,
	bool invert)
{
	@autoreleasepool
	{
		hasCachedDepth.store(false);

		if (!ensureModelLoaded(modelPath))
		{
			CVPixelBufferRelease(inputBuffer);
			return;
		}

		VNCoreMLRequest* request =
			[[VNCoreMLRequest alloc] initWithModel:visionDepthModel];

		request.imageCropAndScaleOption =
			VNImageCropAndScaleOptionScaleFit;

		VNImageRequestHandler* handler =
			[[VNImageRequestHandler alloc]
				initWithCVPixelBuffer:inputBuffer
				options:@{}];

		NSError* error = nil;

		[handler performRequests:@[request] error:&error];

		if (error)
		{
			std::cout
				<< "Apple Depth: VNCoreMLRequest failed: "
				<< [[error localizedDescription] UTF8String]
				<< std::endl;

			CVPixelBufferRelease(inputBuffer);
			return;
		}

		bool foundDepth = false;

		for (VNObservation* observation in request.results)
		{
			if ([observation isKindOfClass:[VNPixelBufferObservation class]])
			{
				VNPixelBufferObservation* pixelObservation =
					(VNPixelBufferObservation*)observation;

				foundDepth =
					cacheDepthFromPixelBuffer(
						pixelObservation.pixelBuffer,
						normalize,
						invert
					);

				if (foundDepth)
				{
					break;
				}
			}
			else if ([observation isKindOfClass:[VNCoreMLFeatureValueObservation class]])
			{
				VNCoreMLFeatureValueObservation* featureObservation =
					(VNCoreMLFeatureValueObservation*)observation;

				MLFeatureValue* featureValue =
					featureObservation.featureValue;

				if (featureValue.multiArrayValue)
				{
					foundDepth =
						cacheDepthFromMultiArray(
							featureValue.multiArrayValue,
							normalize,
							invert
						);

					if (foundDepth)
					{
						break;
					}
				}
				else if (featureValue.imageBufferValue)
				{
					foundDepth =
						cacheDepthFromPixelBuffer(
							featureValue.imageBufferValue,
							normalize,
							invert
						);

					if (foundDepth)
					{
						break;
					}
				}
			}
		}

		if (!foundDepth)
		{
			std::cout
				<< "Apple Depth: No usable depth output found in model results."
				<< std::endl;
		}

		CVPixelBufferRelease(inputBuffer);
	}
}

void
AppleDepth::setupParameters(
	TD::OP_ParameterManager* manager,
	void* reserved1)
{
	{
		TD::OP_NumericParameter np;

		np.name = "Normalize";
		np.label = "Normalize";
		np.page = "Depth";
		np.defaultValues[0] = 1.0;

		manager->appendToggle(np);
	}

	{
		TD::OP_NumericParameter np;

		np.name = "Invert";
		np.label = "Invert";
		np.page = "Depth";
		np.defaultValues[0] = 0.0;

		manager->appendToggle(np);
	}

	{
		TD::OP_StringParameter sp;

		sp.name = "Resolution";
		sp.label = "Output Resolution";
		sp.defaultValue = "Matchinput";
		sp.page = "Depth";

		const char* names[] =
		{
			"Matchinput",
			"Modelnative"
		};

		const char* labels[] =
		{
			"Match Input",
			"Model Native"
		};

		manager->appendMenu(sp, 2, names, labels);
	}

	{
		TD::OP_StringParameter sp;

		sp.name = "Modelfile";
		sp.label = "Model File";
		sp.defaultValue = "";
		sp.page = "Model";

		manager->appendFile(sp);
	}
}

void
AppleDepth::pulsePressed(
	const char* name,
	void* reserved1)
{
}

void
AppleDepth::execute(
	TD::TOP_Output* output,
	const TD::OP_Inputs* inputs,
	void* reserved1)
{
	myExecuteCount++;

	int normalize =
		inputs->getParInt("Normalize");

	int invert =
		inputs->getParInt("Invert");

	int resolutionMode =
		inputs->getParInt("Resolution");

	const char* modelPath =
		inputs->getParFilePath("Modelfile");

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

	ensurePool(width, height);

	CVPixelBufferRef poolBuffer = nullptr;

	CVReturn cvResult =
		CVPixelBufferPoolCreatePixelBuffer(
			nullptr,
			inputPool,
			&poolBuffer
		);

	if (cvResult != kCVReturnSuccess || !poolBuffer)
	{
		return;
	}

	CVPixelBufferLockBaseAddress(poolBuffer, 0);

	uint8_t* destData =
		(uint8_t*)CVPixelBufferGetBaseAddress(poolBuffer);

	size_t destBPR =
		CVPixelBufferGetBytesPerRow(poolBuffer);

	uint8_t* srcData =
		(uint8_t*)downloadResult->getData();

	size_t srcBPR =
		width * 4;

	for (size_t y = 0; y < height; ++y)
	{
		memcpy(
			destData + y * destBPR,
			srcData + y * srcBPR,
			srcBPR
		);
	}

	CVPixelBufferUnlockBaseAddress(poolBuffer, 0);

	runDepth(
		poolBuffer,
		modelPath,
		normalize != 0,
		invert != 0
	);

	if (!hasCachedDepth.load())
	{
		return;
	}

	std::lock_guard<std::mutex> lock(depthMutex);

	size_t depthW =
		cachedDepthW;

	size_t depthH =
		cachedDepthH;

	float* depthData =
		cachedDepthData.data();

	TD::OP_TextureDesc suggestedDesc;
	output->getSuggestedOutputDesc(&suggestedDesc, nullptr);

	size_t commonW =
		(suggestedDesc.width > 0) ? suggestedDesc.width : width;

	size_t commonH =
		(suggestedDesc.height > 0) ? suggestedDesc.height : height;

	size_t outW =
		(resolutionMode == 1) ? depthW : commonW;

	size_t outH =
		(resolutionMode == 1) ? depthH : commonH;

	// The Depth Anything model output may include padding from Vision's
	// VNImageCropAndScaleOptionScaleFit behavior.
	// Model Native shows that raw model output.
	// Match Input crops the valid fitted image area before resizing.
	float sampleX0 = 0.0f;
	float sampleY0 = 0.0f;
	float sampleW =
		(float)depthW;

	float sampleH =
		(float)depthH;

	if (resolutionMode == 0)
	{
		float inputAspect =
			(float)width / (float)height;

		float depthAspect =
			(float)depthW / (float)depthH;

		if (inputAspect < depthAspect)
		{
			// Portrait / narrow input inside a wider model output.
			// Remove horizontal padding.
			sampleH =
				(float)depthH;

			sampleW =
				sampleH * inputAspect;

			sampleX0 =
				((float)depthW - sampleW) * 0.5f;

			sampleY0 =
				0.0f;
		}
		else if (inputAspect > depthAspect)
		{
			// Wide input inside a taller model output.
			// Remove vertical padding.
			sampleW =
				(float)depthW;

			sampleH =
				sampleW / inputAspect;

			sampleX0 =
				0.0f;

			sampleY0 =
				((float)depthH - sampleH) * 0.5f;
		}
	}

	bool sameSize =
		resolutionMode == 1 &&
		depthW == outW &&
		depthH == outH;

	float scaleX = 0.0f;
	float scaleY = 0.0f;

	if (!sameSize)
	{
		scaleX =
			sampleW / (float)outW;

		scaleY =
			sampleH / (float)outH;
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
				depthData + y * depthW;

			float* outRow =
				outData + y * outW;

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
				outData + y * outW;

			float srcYf =
				sampleY0 + (float)y * scaleY;

			if (srcYf < 0.0f)
			{
				srcYf = 0.0f;
			}

			if (srcYf > (float)(depthH - 1))
			{
				srcYf = (float)(depthH - 1);
			}

			size_t y0 =
				(size_t)srcYf;

			size_t y1 =
				(y0 < depthH - 1) ? y0 + 1 : y0;

			float wy =
				srcYf - (float)y0;

			float* row0 =
				depthData + y0 * depthW;

			float* row1 =
				depthData + y1 * depthW;

			for (size_t x = 0; x < outW; ++x)
			{
				float srcXf =
					sampleX0 + (float)x * scaleX;

				if (srcXf < 0.0f)
				{
					srcXf = 0.0f;
				}

				if (srcXf > (float)(depthW - 1))
				{
					srcXf = (float)(depthW - 1);
				}

				size_t x0 =
					(size_t)srcXf;

				size_t x1 =
					(x0 < depthW - 1) ? x0 + 1 : x0;

				float wx =
					srcXf - (float)x0;

				float v00 =
					row0[x0];

				float v01 =
					row0[x1];

				float v10 =
					row1[x0];

				float v11 =
					row1[x1];

				float top =
					v00 * (1.0f - wx) + v01 * wx;

				float bot =
					v10 * (1.0f - wx) + v11 * wx;

				outRow[x] =
					top * (1.0f - wy) + bot * wy;
			}
		}
	}

	TD::TOP_UploadInfo uploadInfo;

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