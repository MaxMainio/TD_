/* Shared Use License: This file is owned by Derivative Inc. (Derivative)
* and can only be used, and/or modified for use, in conjunction with
* Derivative's TouchDesigner software, and only if you are a licensee who has
* accepted Derivative's TouchDesigner license or assignment agreement
* (which also govern the use of this file). You may share or redistribute
* a modified version of this file provided the following conditions are met:
*
* 1. The shared file or redistribution must retain the information set out
* above and this list of conditions.
* 2. Derivative's name (Derivative Inc.) or its trademarks may not be used
* to endorse or promote products derived from this file without specific
* prior written permission from Derivative.
*/

#include "CannyEdgeTOP.h"
#include "Parameters.h"
#include "TOPOutputHelper.h"

#include <cassert>
#include <cstring>
#include <opencv2/core.hpp>
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

	customInfo.opType->setString("Cannyedge");
	customInfo.opLabel->setString("Canny Edge");
	customInfo.authorName->setString("Max Mainio Beidler");
	customInfo.authorEmail->setString("beidler.max@email.com");

	customInfo.minInputs = 1;
	customInfo.maxInputs = 1;
}

DLLEXPORT
TD::TOP_CPlusPlusBase*
CreateTOPInstance(const TD::OP_NodeInfo* info, TD::TOP_Context* context)
{
	return new CannyEdgeTOP(info, context);
}

DLLEXPORT
void
DestroyTOPInstance(TD::TOP_CPlusPlusBase* instance, TD::TOP_Context* context)
{
	delete static_cast<CannyEdgeTOP*>(instance);
}

};

CannyEdgeTOP::CannyEdgeTOP(const TD::OP_NodeInfo*, TD::TOP_Context* context) :
	myFrame{ new cv::Mat() },
	myOutputFrame{ new cv::Mat() },
	myError{ "" },
	myContext{ context },
	myExecuteCount{ 0 }
{
}

CannyEdgeTOP::~CannyEdgeTOP()
{
	delete myFrame;
	delete myOutputFrame;
}

void
CannyEdgeTOP::getGeneralInfo(TD::TOP_GeneralInfo* ginfo, const TD::OP_Inputs*, void*)
{
	ginfo->cookEveryFrame = false;
	ginfo->cookEveryFrameIfAsked = true;
	ginfo->inputSizeIndex = 0;
}

void
CannyEdgeTOP::execute(TD::TOP_Output* output, const TD::OP_Inputs* inputs, void*)
{
	myError = "";
	myExecuteCount++;

	inputTopToMat(inputs);

	if (myFrame->empty())
		return;

	const TD::OP_TOPInput* top = inputs->getInputTOP(0);
	const TD::OP_PixelFormat inputFormat = top
		? top->textureDesc.pixelFormat
		: TD::OP_PixelFormat::Invalid;

	int apertureSize = myParms.evalApperturesize(inputs);
	double lowThresh = myParms.evalLowthreshold(inputs) * 255.0;
	double highThresh = myParms.evalHighthreshold(inputs) * 255.0;
	bool l2grad = myParms.evalL2gradient(inputs);

	if (apertureSize % 2 == 0)
		++apertureSize;

	if (apertureSize < 3)
		apertureSize = 3;

	if (apertureSize > 7)
		apertureSize = 7;

	cv::Canny(*myFrame, *myOutputFrame, lowThresh, highThresh, apertureSize, l2grad);

	TD::OP_TextureDesc desc = TDPlugin::TOPOutput::resolvedDesc(
		output,
		static_cast<uint32_t>(myOutputFrame->cols),
		static_cast<uint32_t>(myOutputFrame->rows),
		TD::OP_PixelFormat::Mono8Fixed,
		true,
		inputs,
		inputFormat);

	TD::TOP_UploadInfo info;
	info.textureDesc = desc;
	info.colorBufferIndex = 0;

	cvMatToOutput(output, info);
}

void
CannyEdgeTOP::setupParameters(TD::OP_ParameterManager* manager, void*)
{
	myParms.setup(manager);
}

void
CannyEdgeTOP::getErrorString(TD::OP_String* error, void*)
{
	error->setString(myError.c_str());
	myError.clear();
}

void
CannyEdgeTOP::inputTopToMat(const TD::OP_Inputs* inputs)
{
	const TD::OP_TOPInput* top = inputs->getInputTOP(0);
	if (!top)
	{
		*myFrame = cv::Mat();
		return;
	}

	TD::OP_TOPInputDownloadOptions opts;
	opts.verticalFlip = true;
	opts.pixelFormat = TD::OP_PixelFormat::RGBA8Fixed;

	TD::OP_SmartRef<TD::OP_TOPDownloadResult> downRes = top->downloadTexture(opts, nullptr);

	if (!downRes)
	{
		*myFrame = cv::Mat();
		return;
	}

	uint8_t* pixel = static_cast<uint8_t*>(downRes->getData());

	if (!pixel)
	{
		*myFrame = cv::Mat();
		return;
	}

	int width = downRes->textureDesc.width;
	int height = downRes->textureDesc.height;

	cv::Mat rgba(height, width, CV_8UC4, pixel);
	cv::cvtColor(rgba, *myFrame, cv::COLOR_RGBA2GRAY);
}

void
CannyEdgeTOP::cvMatToOutput(TD::TOP_Output* output, TD::TOP_UploadInfo info) const
{
	size_t width = info.textureDesc.width;
	size_t height = info.textureDesc.height;
	cv::Mat outMat = *myOutputFrame;

	if (outMat.cols != width || outMat.rows != height)
		cv::resize(outMat, outMat, cv::Size(width, height));

	cv::flip(outMat, outMat, 0);

	TDPlugin::TOPOutput::uploadMono8(
		myContext,
		output,
		outMat.data,
		static_cast<uint32_t>(outMat.step),
		info.textureDesc,
		TD::TOP_FirstPixel::BottomLeft);
}
