#include "AppleSuperResolution.h"
#include <iostream>
#include <cstring>

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

		info->customOPInfo.opType->setString("Applesuperresolution");
		info->customOPInfo.opLabel->setString("Apple Super Resolution");
		info->customOPInfo.opIcon->setString("ASR");

		info->customOPInfo.authorName->setString("Max Mainio Beidler");
		info->customOPInfo.authorEmail->setString("beidler.max@gmail.com");

		info->customOPInfo.minInputs = 1;
		info->customOPInfo.maxInputs = 1;
	}

	DLLEXPORT
	TD::TOP_CPlusPlusBase*
	CreateTOPInstance(const TD::OP_NodeInfo* info, TD::TOP_Context* context)
	{
		return new AppleSuperResolution(info, context);
	}

	DLLEXPORT
	void
	DestroyTOPInstance(TD::TOP_CPlusPlusBase* instance, TD::TOP_Context* context)
	{
		delete (AppleSuperResolution*)instance;
	}
};

AppleSuperResolution::AppleSuperResolution(const TD::OP_NodeInfo* info, TD::TOP_Context* context)
	: myContext(context),
	  myExecuteCount(0)
{
}

AppleSuperResolution::~AppleSuperResolution()
{
	clearVideoSession();

	if (inputPool)
	{
		CVPixelBufferPoolRelease(inputPool);
	}

	if (outputPool)
	{
		CVPixelBufferPoolRelease(outputPool);
	}
}

void
AppleSuperResolution::getGeneralInfo(
	TD::TOP_GeneralInfo* ginfo,
	const TD::OP_Inputs* inputs,
	void* reserved1)
{
	ginfo->cookEveryFrame = false;
	ginfo->cookEveryFrameIfAsked = false;
}

void
AppleSuperResolution::ensureInputPool(size_t width, size_t height)
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
AppleSuperResolution::ensureOutputPool(size_t width, size_t height)
{
	if (outputPool &&
		outputPoolW == width &&
		outputPoolH == height)
	{
		return;
	}

	if (outputPool)
	{
		CVPixelBufferPoolRelease(outputPool);
		outputPool = nullptr;
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
		&outputPool
	);

	outputPoolW = width;
	outputPoolH = height;
}

void
AppleSuperResolution::clearVideoHistory()
{
	if (previousInputBuffer)
	{
		CVPixelBufferRelease(previousInputBuffer);
		previousInputBuffer = nullptr;
	}

	if (previousOutputBuffer)
	{
		CVPixelBufferRelease(previousOutputBuffer);
		previousOutputBuffer = nullptr;
	}

	historyInputW = 0;
	historyInputH = 0;
	historyOutputW = 0;
	historyOutputH = 0;
	historyScaleFactor = 0;
	historyInputType = 0;
}

void
AppleSuperResolution::clearVideoSession()
{
	clearVideoHistory();

#ifdef __OBJC__
	if (videoProcessor && videoSessionActive)
	{
		[videoProcessor endSession];
	}
#endif

	videoProcessor = nullptr;
	videoConfig = nullptr;
	videoSessionActive = false;

	sessionInputW = 0;
	sessionInputH = 0;
	sessionScaleFactor = 0;
	sessionInputType = 0;
}

CVPixelBufferRef
AppleSuperResolution::runSuperResolution(
	CVPixelBufferRef inputBuffer,
	size_t width,
	size_t height,
	int requestedScaleFactor,
	int inputTypeIndex)
{
	@autoreleasepool
	{
		// Runs Apple VideoToolbox Super Resolution on the current input frame.
		// In Image mode, each cook is processed independently.
		// In Video mode, the operator keeps previous input/output frames for temporal context.
		// The inputBuffer is a BGRA CVPixelBuffer copied from the TouchDesigner input.
		// The output is a new BGRA CVPixelBuffer at the selected / supported scale factor.

		bool useVideoMode =
			(inputTypeIndex == 1);

		VTSuperResolutionScalerConfigurationInputType inputType =
			useVideoMode
				? VTSuperResolutionScalerConfigurationInputTypeVideo
				: VTSuperResolutionScalerConfigurationInputTypeImage;

		if (![VTSuperResolutionScalerConfiguration isSupported])
		{
			std::cout << "Apple Super Resolution is not supported on this system." << std::endl;
			return nullptr;
		}

		NSArray<NSNumber*>* supportedScaleFactors =
			[VTSuperResolutionScalerConfiguration supportedScaleFactors];

		NSInteger scaleFactor =
			(NSInteger)requestedScaleFactor;

		bool scaleFactorSupported = false;

		for (NSNumber* n in supportedScaleFactors)
		{
			if ([n integerValue] == scaleFactor)
			{
				scaleFactorSupported = true;
				break;
			}
		}

		if (!scaleFactorSupported)
		{
			if (supportedScaleFactors.count > 0)
			{
				NSInteger fallbackScaleFactor =
					[supportedScaleFactors[0] integerValue];

				std::cout
					<< "Requested scale factor "
					<< scaleFactor
					<< "x is not supported. "
					<< "Using "
					<< fallbackScaleFactor
					<< "x instead."
					<< std::endl;

				scaleFactor = fallbackScaleFactor;
			}
			else
			{
				std::cout
					<< "No supported scale factors reported by VideoToolbox."
					<< std::endl;

				return nullptr;
			}
		}

		// VideoToolbox Super Resolution has input-size limits.
		// On macOS, image input is expected to fit within 1920 x 1920.
		// If the input is larger, configuration creation can fail.
		if (width > 1920 || height > 1920)
		{
			std::cout
				<< "Apple Super Resolution input is too large. "
				<< "Input was "
				<< width
				<< " x "
				<< height
				<< ". Maximum supported image input is 1920 x 1920."
				<< std::endl;

			return nullptr;
		}

		size_t outW = width * scaleFactor;
		size_t outH = height * scaleFactor;

		// Video mode depends on previous-frame history.
		// If the input size, output size, scale factor, or input type changes,
		// the stored history no longer matches the current processing session.
		if (useVideoMode)
		{
			bool historyMatches =
				previousInputBuffer &&
				previousOutputBuffer &&
				historyInputW == width &&
				historyInputH == height &&
				historyOutputW == outW &&
				historyOutputH == outH &&
				historyScaleFactor == scaleFactor &&
				historyInputType == inputTypeIndex;

			if (!historyMatches)
			{
				clearVideoHistory();
			}
		}
		else
		{
			clearVideoHistory();
		}

		ensureOutputPool(outW, outH);

		CVPixelBufferRef outputBuffer = nullptr;

		CVReturn cvResult =
			CVPixelBufferPoolCreatePixelBuffer(
				nullptr,
				outputPool,
				&outputBuffer
			);

		if (cvResult != kCVReturnSuccess || !outputBuffer)
		{
			std::cout << "SR failed at: output pixel buffer creation" << std::endl;
			return nullptr;
		}

		NSError* error = nil;

		VTSuperResolutionScalerConfiguration* config =
			[[VTSuperResolutionScalerConfiguration alloc]
				initWithFrameWidth:(NSInteger)width
				frameHeight:(NSInteger)height
				scaleFactor:(NSInteger)scaleFactor
				inputType:inputType
				usePrecomputedFlow:NO
				qualityPrioritization:VTSuperResolutionScalerConfigurationQualityPrioritizationNormal
				revision:VTSuperResolutionScalerConfigurationRevision1];

		if (!config)
		{
			std::cout
				<< "Failed to create Apple Super Resolution configuration. "
				<< "Input: "
				<< width
				<< " x "
				<< height
				<< ", scale factor: "
				<< scaleFactor
				<< "x."
				<< std::endl;

			CVPixelBufferRelease(outputBuffer);
			return nullptr;
		}

		if ([config configurationModelStatus] ==
			VTSuperResolutionScalerConfigurationModelStatusDownloadRequired)
		{
			std::cout
				<< "Apple Super Resolution model download required. "
				<< "Press Download Model."
				<< std::endl;

			CVPixelBufferRelease(outputBuffer);
			return nullptr;
		}

		if ([config configurationModelStatus] ==
			VTSuperResolutionScalerConfigurationModelStatusDownloading)
		{
			std::cout
				<< "Apple Super Resolution model is still downloading."
				<< std::endl;

			CVPixelBufferRelease(outputBuffer);
			return nullptr;
		}

		// Image mode creates a one-shot VTFrameProcessor session per cook.
		// Video mode reuses a persistent VTFrameProcessor session across cooks,
		// rebuilding only when the input size, scale factor, or input type changes.

		VTFrameProcessor* processor = nullptr;

		if (useVideoMode)
		{
			bool sessionMatches =
				videoSessionActive &&
				videoProcessor &&
				sessionInputW == width &&
				sessionInputH == height &&
				sessionScaleFactor == scaleFactor &&
				sessionInputType == inputTypeIndex;

			if (!sessionMatches)
			{
				clearVideoSession();

				videoConfig = config;
				videoProcessor = [[VTFrameProcessor alloc] init];

				if (!videoProcessor)
				{
					CVPixelBufferRelease(outputBuffer);
					return nullptr;
				}

				if (![videoProcessor startSessionWithConfiguration:videoConfig
															error:&error])
				{
					if (error)
					{
						std::cout
							<< "VTFrameProcessor video startSession error: "
							<< [[error localizedDescription] UTF8String]
							<< std::endl;
					}

					videoProcessor = nullptr;
					videoConfig = nullptr;
					videoSessionActive = false;

					CVPixelBufferRelease(outputBuffer);
					return nullptr;
				}

				videoSessionActive = true;
				sessionInputW = width;
				sessionInputH = height;
				sessionScaleFactor = (int)scaleFactor;
				sessionInputType = inputTypeIndex;
			}

			processor = videoProcessor;
		}
		else
		{
			clearVideoSession();

			processor = [[VTFrameProcessor alloc] init];

			if (!processor)
			{
				CVPixelBufferRelease(outputBuffer);
				return nullptr;
			}

			if (![processor startSessionWithConfiguration:config
													error:&error])
			{
				if (error)
				{
					std::cout
						<< "VTFrameProcessor image startSession error: "
						<< [[error localizedDescription] UTF8String]
						<< std::endl;
				}

				CVPixelBufferRelease(outputBuffer);
				return nullptr;
			}
		}

		CMTime pts =
			CMTimeMake(myExecuteCount, 60);

		CMTime previousPts =
			CMTimeMake(myExecuteCount - 1, 60);

		VTFrameProcessorFrame* sourceFrame =
			[[VTFrameProcessorFrame alloc]
				initWithBuffer:inputBuffer
				presentationTimeStamp:pts];

		VTFrameProcessorFrame* destinationFrame =
			[[VTFrameProcessorFrame alloc]
				initWithBuffer:outputBuffer
				presentationTimeStamp:pts];

		VTFrameProcessorFrame* previousFrame = nil;
		VTFrameProcessorFrame* previousOutputFrame = nil;

		if (useVideoMode && previousInputBuffer && previousOutputBuffer)
		{
			previousFrame =
				[[VTFrameProcessorFrame alloc]
					initWithBuffer:previousInputBuffer
					presentationTimeStamp:previousPts];

			previousOutputFrame =
				[[VTFrameProcessorFrame alloc]
					initWithBuffer:previousOutputBuffer
					presentationTimeStamp:previousPts];
		}

		if (!sourceFrame || !destinationFrame)
		{
			if (!useVideoMode)
			{
				[processor endSession];
			}

			CVPixelBufferRelease(outputBuffer);
			return nullptr;
		}

		VTSuperResolutionScalerParametersSubmissionMode submissionMode =
			useVideoMode
				? VTSuperResolutionScalerParametersSubmissionModeSequential
				: VTSuperResolutionScalerParametersSubmissionModeRandom;

		VTSuperResolutionScalerParameters* params =
			[[VTSuperResolutionScalerParameters alloc]
				initWithSourceFrame:sourceFrame
				previousFrame:previousFrame
				previousOutputFrame:previousOutputFrame
				opticalFlow:nil
				submissionMode:submissionMode
				destinationFrame:destinationFrame];

		if (!params)
		{
			if (!useVideoMode)
			{
				[processor endSession];
			}

			CVPixelBufferRelease(outputBuffer);
			return nullptr;
		}

		if (![processor processWithParameters:params
								error:&error])
		{
			if (error)
			{
				std::cout
					<< "VTFrameProcessor process error: "
					<< [[error localizedDescription] UTF8String]
					<< std::endl;
			}

			if (useVideoMode)
			{
				clearVideoSession();
			}
			else
			{
				[processor endSession];
			}

			CVPixelBufferRelease(outputBuffer);
			return nullptr;
		}

		if (useVideoMode)
		{
			clearVideoHistory();

			previousInputBuffer = inputBuffer;
			CVPixelBufferRetain(previousInputBuffer);

			previousOutputBuffer = outputBuffer;
			CVPixelBufferRetain(previousOutputBuffer);

			historyInputW = width;
			historyInputH = height;
			historyOutputW = outW;
			historyOutputH = outH;
			historyScaleFactor = (int)scaleFactor;
			historyInputType = inputTypeIndex;
		}
		else
		{
			clearVideoHistory();
		}

		if (!useVideoMode)
		{
			[processor endSession];
		}

		return outputBuffer;
	}
}

void
AppleSuperResolution::setupParameters(
	TD::OP_ParameterManager* manager,
	void* reserved1)
{
	{
		TD::OP_StringParameter sp;

		sp.name = "Scalefactor";
		sp.label = "Scale Factor";
		sp.defaultValue = "4x";
		sp.page = "Super Resolution";

		const char* names[] =
		{
			"2x",
			"3x",
			"4x"
		};

		const char* labels[] =
		{
			"2x",
			"3x",
			"4x"
		};

		manager->appendMenu(sp, 3, names, labels);
	}

	{
		TD::OP_StringParameter sp;

		sp.name = "Inputtype";
		sp.label = "Input Type";
		sp.defaultValue = "Image";
		sp.page = "Super Resolution";

		const char* names[] =
		{
			"Image",
			"Video"
		};

		const char* labels[] =
		{
			"Image",
			"Video"
		};

		manager->appendMenu(sp, 2, names, labels);
	}

	{
		TD::OP_NumericParameter np;

		np.name = "Resetvideo";
		np.label = "Reset Video";
		np.page = "Super Resolution";

		manager->appendPulse(np);
	}

	{
		TD::OP_NumericParameter np;

		np.name = "Downloadmodel";
		np.label = "Download Model";
		np.page = "Model";

		manager->appendPulse(np);
	}
}

void
AppleSuperResolution::pulsePressed(
	const char* name,
	void* reserved1)
{
	if (strcmp(name, "Resetvideo") == 0)
	{
		clearVideoSession();
		std::cout << "Apple Super Resolution video session reset." << std::endl;
		return;
	}

	if (strcmp(name, "Downloadmodel") == 0)
	{
		NSInteger scaleFactor = 4;

		VTSuperResolutionScalerConfiguration* config =
			[[VTSuperResolutionScalerConfiguration alloc]
				initWithFrameWidth:512
				frameHeight:512
				scaleFactor:scaleFactor
				inputType:VTSuperResolutionScalerConfigurationInputTypeImage
				usePrecomputedFlow:NO
				qualityPrioritization:VTSuperResolutionScalerConfigurationQualityPrioritizationNormal
				revision:VTSuperResolutionScalerConfigurationRevision1];

		if (!config)
		{
			std::cout << "Could not create SR config for model download." << std::endl;
			return;
		}

		std::cout << "Starting Apple Super Resolution model download..." << std::endl;

		[config downloadConfigurationModelWithCompletionHandler:^(NSError* error)
		{
			if (error)
			{
				std::cout
					<< "Apple Super Resolution model download failed: "
					<< [[error localizedDescription] UTF8String]
					<< std::endl;
			}
			else
			{
				std::cout
					<< "Apple Super Resolution model download completed."
					<< std::endl;
			}
		}];
	}
}

void
AppleSuperResolution::execute(
	TD::TOP_Output* output,
	const TD::OP_Inputs* inputs,
	void* reserved1)
{
	myExecuteCount++;

	int inputTypeIndex =
		inputs->getParInt("Inputtype");

	int scaleIndex =
		inputs->getParInt("Scalefactor");

	int requestedScaleFactor = 4;

	switch (scaleIndex)
	{
		case 0:
			requestedScaleFactor = 2;
			break;

		case 1:
			requestedScaleFactor = 3;
			break;

		case 2:
		default:
			requestedScaleFactor = 4;
			break;
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

	// Download the input TOP into CPU memory as BGRA8Fixed.
	// VideoToolbox receives the image as a BGRA CVPixelBuffer.
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

	// Create or reuse an input CVPixelBuffer matching the source image size.
	ensureInputPool(width, height);

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

	// Copy TouchDesigner BGRA data into the input CVPixelBuffer.
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

	// Run Apple VideoToolbox Super Resolution.
	// On success, this returns a retained CVPixelBuffer containing the upscaled image.
	CVPixelBufferRef srBuffer =
		runSuperResolution(
			poolBuffer,
			width,
			height,
			requestedScaleFactor,
			inputTypeIndex
		);

	CVPixelBufferRelease(poolBuffer);

	if (!srBuffer)
	{
		return;
	}

	size_t outW =
		CVPixelBufferGetWidth(srBuffer);

	size_t outH =
		CVPixelBufferGetHeight(srBuffer);

	size_t outBPR =
		CVPixelBufferGetBytesPerRow(srBuffer);

	uint64_t outSize =
		outW * outH * 4;

	TD::OP_SmartRef<TD::TOP_Buffer> outBuffer =
		myContext->createOutputBuffer(
			outSize,
			TD::TOP_BufferFlags::None,
			nullptr
		);

	// Copy the VideoToolbox output CVPixelBuffer into TouchDesigner's CPU output buffer.
	CVPixelBufferLockBaseAddress(
		srBuffer,
		kCVPixelBufferLock_ReadOnly
	);

	uint8_t* srcSR =
		(uint8_t*)CVPixelBufferGetBaseAddress(srBuffer);

	uint8_t* outData =
		(uint8_t*)outBuffer->data;

	for (size_t y = 0; y < outH; ++y)
	{
		memcpy(
			outData + y * outW * 4,
			srcSR + y * outBPR,
			outW * 4
		);
	}

	CVPixelBufferUnlockBaseAddress(
		srBuffer,
		kCVPixelBufferLock_ReadOnly
	);

	CVPixelBufferRelease(srBuffer);

	// Upload the upscaled BGRA image back into TouchDesigner as a TOP texture.
	TD::TOP_UploadInfo uploadInfo;

	uploadInfo.textureDesc.width =
		(uint32_t)outW;

	uploadInfo.textureDesc.height =
		(uint32_t)outH;

	uploadInfo.textureDesc.texDim =
		TD::OP_TexDim::e2D;

	uploadInfo.textureDesc.pixelFormat =
		TD::OP_PixelFormat::BGRA8Fixed;

	uploadInfo.firstPixel =
		TD::TOP_FirstPixel::TopLeft;

	output->uploadBuffer(
		&outBuffer,
		uploadInfo,
		nullptr
	);
}