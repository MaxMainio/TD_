#pragma once

#include "TOP_CPlusPlusBase.h"

#include <vector>
#include <mutex>
#include <atomic>
#include <cstring>

#ifdef __OBJC__
#import <Vision/Vision.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#endif

class AppleOpticalFlow : public TD::TOP_CPlusPlusBase
{
public:
	AppleOpticalFlow(const TD::OP_NodeInfo* info, TD::TOP_Context* context);
	virtual ~AppleOpticalFlow();

	virtual void getGeneralInfo(TD::TOP_GeneralInfo* ginfo, const TD::OP_Inputs* inputs, void* reserved1) override;
	virtual void execute(TD::TOP_Output* output, const TD::OP_Inputs* inputs, void* reserved1) override;

	virtual void setupParameters(TD::OP_ParameterManager* manager, void* reserved1) override;
	virtual void pulsePressed(const char* name, void* reserved1) override;

private:
	void ensureInputPool(size_t width, size_t height);

	void clearHistory();

	bool runOpticalFlow(
		CVPixelBufferRef previousBuffer,
		CVPixelBufferRef currentBuffer,
		int accuracyIndex,
		bool normalize,
		float normalizeScale);

	bool cacheFlowFromPixelBuffer(
		CVPixelBufferRef flowBuffer,
		bool normalize,
		float normalizeScale);

	static float halfToFloat(uint16_t h);

	TD::TOP_Context* myContext;
	int32_t myExecuteCount;

	CVPixelBufferPoolRef inputPool = nullptr;
	size_t inputPoolW = 0;
	size_t inputPoolH = 0;

	// Retained copy of the previous cook's input frame.
	// Optical flow needs two same-sized frames: previous + current.
	CVPixelBufferRef previousInputBuffer = nullptr;
	size_t previousInputW = 0;
	size_t previousInputH = 0;

	std::mutex flowMutex;
	std::atomic<bool> hasCachedFlow{false};

	std::vector<float> cachedFlowData;
	size_t cachedFlowW = 0;
	size_t cachedFlowH = 0;
};