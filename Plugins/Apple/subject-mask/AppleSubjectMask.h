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

class AppleSubjectMask : public TD::TOP_CPlusPlusBase
{
public:
	AppleSubjectMask(const TD::OP_NodeInfo* info, TD::TOP_Context* context);
	virtual ~AppleSubjectMask();

	virtual void		getGeneralInfo(TD::TOP_GeneralInfo* ginfo, const TD::OP_Inputs* inputs, void* reserved1) override;
	virtual void		execute(TD::TOP_Output* output, const TD::OP_Inputs* inputs, void* reserved1) override;

	virtual void		setupParameters(TD::OP_ParameterManager* manager, void* reserved1) override;
	virtual void		pulsePressed(const char* name, void* reserved1) override;

private:
	void				runSegmentation(CVPixelBufferRef inputBuffer, int qualityLevel);
	void				ensurePool(size_t width, size_t height);

#ifdef __OBJC__
	VNGeneratePersonSegmentationRequest* segmentationRequest;
#else
	void* segmentationRequest;
#endif

	TD::TOP_Context*	myContext;
	int32_t				myExecuteCount;
	int					currentQualityLevel;

	// Cached mask state.
	std::mutex			maskMutex;
	std::atomic<bool>	hasCachedMask{false};

	// Cached mask result (protected by maskMutex)
	std::vector<uint8_t> cachedMaskData;
	size_t				cachedMaskW = 0;
	size_t				cachedMaskH = 0;

	// CVPixelBuffer pool for input frames
	CVPixelBufferPoolRef inputPool = nullptr;
	size_t				inputPoolW = 0;
	size_t				inputPoolH = 0;
};