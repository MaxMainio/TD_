#pragma once

#include "TOP_CPlusPlusBase.h"

#ifdef __OBJC__
#import <VideoToolbox/VideoToolbox.h>
#import <CoreVideo/CoreVideo.h>
#import <CoreMedia/CoreMedia.h>
#import <Foundation/Foundation.h>
#endif

class AppleSuperResolution : public TD::TOP_CPlusPlusBase
{
public:
	AppleSuperResolution(const TD::OP_NodeInfo* info, TD::TOP_Context* context);
	virtual ~AppleSuperResolution();

	virtual void getGeneralInfo(TD::TOP_GeneralInfo* ginfo, const TD::OP_Inputs* inputs, void* reserved1) override;
	virtual void execute(TD::TOP_Output* output, const TD::OP_Inputs* inputs, void* reserved1) override;

	virtual void setupParameters(TD::OP_ParameterManager* manager, void* reserved1) override;
	virtual void pulsePressed(const char* name, void* reserved1) override;

private:
	void ensureInputPool(size_t width, size_t height);
	void ensureOutputPool(size_t width, size_t height);

	void clearVideoHistory();
	void clearVideoSession();

	CVPixelBufferRef runSuperResolution(
		CVPixelBufferRef inputBuffer,
		size_t width,
		size_t height,
		int requestedScaleFactor,
		int inputTypeIndex);

	TD::TOP_Context* myContext;
	int32_t myExecuteCount;

	CVPixelBufferPoolRef inputPool = nullptr;
	size_t inputPoolW = 0;
	size_t inputPoolH = 0;

	CVPixelBufferPoolRef outputPool = nullptr;
	size_t outputPoolW = 0;
	size_t outputPoolH = 0;

	// Stored frame history for Video input mode.
	// Image mode clears this history and treats every cook independently.
	CVPixelBufferRef previousInputBuffer = nullptr;
	CVPixelBufferRef previousOutputBuffer = nullptr;

	size_t historyInputW = 0;
	size_t historyInputH = 0;
	size_t historyOutputW = 0;
	size_t historyOutputH = 0;
	int historyScaleFactor = 0;
	int historyInputType = 0;

#ifdef __OBJC__
	// Cached VideoToolbox session objects used only in Video input mode.
	// Image mode continues to use a one-shot processor per cook.
	VTFrameProcessor* videoProcessor = nullptr;
	VTSuperResolutionScalerConfiguration* videoConfig = nullptr;
#else
	void* videoProcessor = nullptr;
	void* videoConfig = nullptr;
#endif

	bool videoSessionActive = false;

	size_t sessionInputW = 0;
	size_t sessionInputH = 0;
	int sessionScaleFactor = 0;
	int sessionInputType = 0;
};