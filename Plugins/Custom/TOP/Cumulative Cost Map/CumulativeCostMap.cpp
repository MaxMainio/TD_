#include "CumulativeCostMap.h"
#include "Parameters.h"
#include "TOPOutputHelper.h"

#include <cassert>
#include <cstring>
#include <algorithm>
#include <limits>
#include <opencv2/imgproc.hpp>

extern "C"
{

DLLEXPORT
void
FillTOPPluginInfo(TD::TOP_PluginInfo* info)
{
	if (!info->setAPIVersion(TD::TOPCPlusPlusAPIVersion))
		return;

	info->executeMode = TD::TOP_ExecuteMode::CPUMem;

	TD::OP_CustomOPInfo& customInfo = info->customOPInfo;

	customInfo.opType->setString("Cumulativecostmap");
	customInfo.opLabel->setString("Cumulative Cost Map");
	customInfo.authorName->setString("Max Mainio Beidler");
	customInfo.authorEmail->setString("beidler.max@email.com");

	customInfo.minInputs = 1;
	customInfo.maxInputs = 1;
}

DLLEXPORT
TD::TOP_CPlusPlusBase*
CreateTOPInstance(const TD::OP_NodeInfo* info, TD::TOP_Context* context)
{
	return new CumulativeCostMap(info, context);
}

DLLEXPORT
void
DestroyTOPInstance(TD::TOP_CPlusPlusBase* instance, TD::TOP_Context* context)
{
	delete static_cast<CumulativeCostMap*>(instance);
}

};

CumulativeCostMap::CumulativeCostMap(const TD::OP_NodeInfo*, TD::TOP_Context* context) :
	myInputFrame{ new cv::Mat() },
	myCostFrame{ new cv::Mat() },
	myError{ "" },
	myContext{ context },
	myExecuteCount{ 0 }
{
}

CumulativeCostMap::~CumulativeCostMap()
{
	delete myInputFrame;
	delete myCostFrame;
}

void
CumulativeCostMap::getGeneralInfo(TD::TOP_GeneralInfo* ginfo, const TD::OP_Inputs*, void*)
{
	ginfo->cookEveryFrame = false;
	ginfo->cookEveryFrameIfAsked = true;
	ginfo->inputSizeIndex = 0;
}

void
CumulativeCostMap::execute(TD::TOP_Output* output, const TD::OP_Inputs* inputs, void*)
{
	myError = "";
	myExecuteCount++;

	inputTopToMat(inputs);

	if (myInputFrame->empty())
		return;

	const TD::OP_TOPInput* top = inputs->getInputTOP(0);
	const TD::OP_PixelFormat inputFormat = top
		? top->textureDesc.pixelFormat
		: TD::OP_PixelFormat::Invalid;

	processCost(inputs);

	if (myCostFrame->empty())
		return;

	TD::OP_TextureDesc desc = TDPlugin::TOPOutput::resolvedDesc(
		output,
		static_cast<uint32_t>(myCostFrame->cols),
		static_cast<uint32_t>(myCostFrame->rows),
		TD::OP_PixelFormat::Mono32Float,
		true,
		inputs,
		inputFormat);

	TD::TOP_UploadInfo info;
	info.textureDesc = desc;
	info.colorBufferIndex = 0;
	cvMatToOutput(output, info);
}

void
CumulativeCostMap::setupParameters(TD::OP_ParameterManager* manager, void*)
{
	myParms.setup(manager);
}

void
CumulativeCostMap::getErrorString(TD::OP_String* error, void*)
{
	error->setString(myError.c_str());
	myError.clear();
}

void
CumulativeCostMap::inputTopToMat(const TD::OP_Inputs* inputs)
{
	const TD::OP_TOPInput* top = inputs->getInputTOP(0);
	if (!top)
	{
		*myInputFrame = cv::Mat();
		return;
	}

	TD::OP_TOPInputDownloadOptions opts;
	opts.verticalFlip = true;
	opts.pixelFormat = TD::OP_PixelFormat::RGBA32Float;

	TD::OP_SmartRef<TD::OP_TOPDownloadResult> downRes = top->downloadTexture(opts, nullptr);

	if (!downRes)
	{
		*myInputFrame = cv::Mat();
		return;
	}

	float* pixel = static_cast<float*>(downRes->getData());

	if (!pixel)
	{
		*myInputFrame = cv::Mat();
		return;
	}

	int width = downRes->textureDesc.width;
	int height = downRes->textureDesc.height;

	cv::Mat rgba(height, width, CV_32FC4, pixel);

	// Clone so data remains valid after downRes goes out of scope.
	*myInputFrame = rgba.clone();
}

void
CumulativeCostMap::processCost(const TD::OP_Inputs* inputs)
{
	const int width = myInputFrame->cols;
	const int height = myInputFrame->rows;

	if (width <= 0 || height <= 0)
	{
		*myCostFrame = cv::Mat();
		return;
	}

	const ModeMenuItems mode = myParms.evalMode(inputs);
	const ChannelMenuItems channel = myParms.evalChannel(inputs);

	int channelIndex = 0;
	switch (channel)
	{
	case ChannelMenuItems::Red:
		channelIndex = 0;
		break;
	case ChannelMenuItems::Green:
		channelIndex = 1;
		break;
	case ChannelMenuItems::Blue:
		channelIndex = 2;
		break;
	case ChannelMenuItems::Alpha:
		channelIndex = 3;
		break;
	}

	*myCostFrame = cv::Mat(height, width, CV_32FC1, cv::Scalar(0.0f));

	// Bottom row initializes directly from input cost.
	const int bottomY = height - 1;

	const cv::Vec4f* bottomIn = myInputFrame->ptr<cv::Vec4f>(bottomY);
	float* bottomCost = myCostFrame->ptr<float>(bottomY);

	for (int x = 0; x < width; ++x)
	{
		bottomCost[x] = bottomIn[x][channelIndex];
	}

	// Accumulate from bottom to top.
	for (int y = height - 2; y >= 0; --y)
	{
		const cv::Vec4f* inRow = myInputFrame->ptr<cv::Vec4f>(y);
		const float* belowRow = myCostFrame->ptr<float>(y + 1);
		float* costRow = myCostFrame->ptr<float>(y);

		for (int x = 0; x < width; ++x)
		{
			float best = belowRow[x];

			if (x > 0)
			{
				if (mode == ModeMenuItems::Minimum)
					best = std::min(best, belowRow[x - 1]);
				else
					best = std::max(best, belowRow[x - 1]);
			}

			if (x < width - 1)
			{
				if (mode == ModeMenuItems::Minimum)
					best = std::min(best, belowRow[x + 1]);
				else
					best = std::max(best, belowRow[x + 1]);
			}

			costRow[x] = inRow[x][channelIndex] + best;
		}
	}
}

void
CumulativeCostMap::cvMatToOutput(TD::TOP_Output* output, TD::TOP_UploadInfo info) const
{
	size_t width = info.textureDesc.width;
	size_t height = info.textureDesc.height;
	cv::Mat outMat = *myCostFrame;

	if (outMat.cols != width || outMat.rows != height)
		cv::resize(outMat, outMat, cv::Size(width, height));

	cv::flip(outMat, outMat, 0);

	TDPlugin::TOPOutput::uploadMono32(
		myContext,
		output,
		static_cast<const float*>(static_cast<const void*>(outMat.data)),
		static_cast<uint32_t>(outMat.step),
		info.textureDesc,
		TD::TOP_FirstPixel::BottomLeft);
}
