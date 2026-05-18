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

class AppleSaliencyMap : public TD::TOP_CPlusPlusBase
{
public:
	AppleSaliencyMap(const TD::OP_NodeInfo* info, TD::TOP_Context* context);
	virtual ~AppleSaliencyMap();

	virtual void		getGeneralInfo(TD::TOP_GeneralInfo* ginfo, const TD::OP_Inputs* inputs, void* reserved1) override;
	virtual void		execute(TD::TOP_Output* output, const TD::OP_Inputs* inputs, void* reserved1) override;

	virtual void		setupParameters(TD::OP_ParameterManager* manager, void* reserved1) override;
	virtual void		pulsePressed(const char* name, void* reserved1) override;

private:
	void				runSaliency(CVPixelBufferRef inputBuffer, int mode);
	void				ensurePool(size_t width, size_t height);

	TD::TOP_Context*	myContext;
	int32_t				myExecuteCount;
	int					currentMode;

	std::mutex			heatmapMutex;
	std::atomic<bool>	hasCachedHeatmap{false};

	std::vector<float>	cachedHeatmapData;
	size_t				cachedHeatmapW = 0;
	size_t				cachedHeatmapH = 0;

	CVPixelBufferPoolRef inputPool = nullptr;
	size_t				inputPoolW = 0;
	size_t				inputPoolH = 0;
};