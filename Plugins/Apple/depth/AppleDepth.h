#pragma once

#include "TOP_CPlusPlusBase.h"

#include <vector>
#include <mutex>
#include <atomic>
#include <string>

#ifdef __OBJC__
#import <Vision/Vision.h>
#import <CoreML/CoreML.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#endif

class AppleDepth : public TD::TOP_CPlusPlusBase
{
public:
	AppleDepth(const TD::OP_NodeInfo* info, TD::TOP_Context* context);
	virtual ~AppleDepth();

	virtual void getGeneralInfo(TD::TOP_GeneralInfo* ginfo, const TD::OP_Inputs* inputs, void* reserved1) override;
	virtual void execute(TD::TOP_Output* output, const TD::OP_Inputs* inputs, void* reserved1) override;

	virtual void setupParameters(TD::OP_ParameterManager* manager, void* reserved1) override;
	virtual void pulsePressed(const char* name, void* reserved1) override;

private:
	void ensurePool(size_t width, size_t height);

	bool ensureModelLoaded(const char* modelPath);
	void unloadModel();

	void runDepth(
		CVPixelBufferRef inputBuffer,
		const char* modelPath,
		bool normalize,
		bool invert);

	bool cacheDepthFromMultiArray(
		MLMultiArray* array,
		bool normalize,
		bool invert);

	bool cacheDepthFromPixelBuffer(
		CVPixelBufferRef pixelBuffer,
		bool normalize,
		bool invert);

	static float halfToFloat(uint16_t h);

	TD::TOP_Context* myContext;
	int32_t myExecuteCount;

	std::mutex depthMutex;
	std::atomic<bool> hasCachedDepth{false};

	std::vector<float> cachedDepthData;
	size_t cachedDepthW = 0;
	size_t cachedDepthH = 0;

	CVPixelBufferPoolRef inputPool = nullptr;
	size_t inputPoolW = 0;
	size_t inputPoolH = 0;

	std::string currentModelPath;
	bool modelLoaded = false;

#ifdef __OBJC__
	__strong MLModel* depthModel = nil;
	__strong VNCoreMLModel* visionDepthModel = nil;
#else
	void* depthModel = nullptr;
	void* visionDepthModel = nullptr;
#endif
};