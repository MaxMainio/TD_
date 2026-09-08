#pragma once

#include "TOP_CPlusPlusBase.h"
#include <vector>
#include <mutex>
#include <atomic>

#ifdef __OBJC__
#import <Vision/Vision.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#endif

class AppleInstanceMask : public TD::TOP_CPlusPlusBase
{
public:
	AppleInstanceMask(const TD::OP_NodeInfo* info, TD::TOP_Context* context);
	virtual ~AppleInstanceMask();

	virtual void getGeneralInfo(TD::TOP_GeneralInfo* ginfo, const TD::OP_Inputs* inputs, void* reserved1) override;
	virtual void execute(TD::TOP_Output* output, const TD::OP_Inputs* inputs, void* reserved1) override;

	virtual void setupParameters(TD::OP_ParameterManager* manager, void* reserved1) override;
	virtual void pulsePressed(const char* name, void* reserved1) override;

private:
	void runInstanceMask(CVPixelBufferRef inputBuffer, int instanceIndex);
	void ensurePool(size_t width, size_t height);

	TD::TOP_Context* myContext;
	int32_t myExecuteCount;

	std::mutex maskMutex;
	std::atomic<bool> hasCachedMask{false};

	std::vector<float> cachedMaskData;
	size_t cachedMaskW = 0;
	size_t cachedMaskH = 0;

	CVPixelBufferPoolRef inputPool = nullptr;
	size_t inputPoolW = 0;
	size_t inputPoolH = 0;
};