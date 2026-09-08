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

#include "DistanceTransformTOP.h"
#include "Parameters.h"
#include "TOPOutputHelper.h"

#include <cassert>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>



// These functions are basic C function, which the DLL loader can find
// much easier than finding a C++ Class.
// The DLLEXPORT prefix is needed so the compile exports these functions from the .dll
// you are creating
extern "C"
{

DLLEXPORT
void
FillTOPPluginInfo(TD::TOP_PluginInfo* info)
{
	// This must always be set to this constant
	if (!info->setAPIVersion(TD::TOPCPlusPlusAPIVersion))
		return;

	// Change this to change the executeMode behavior of this plugin.
	info->executeMode = TD::TOP_ExecuteMode::CPUMem;

	// For more information on OP_CustomOPInfo see CPlusPlus_Common.h
	TD::OP_CustomOPInfo& customInfo = info->customOPInfo;

	// Unique name of the node which starts with an upper case letter, followed by lower case letters or numbers
	customInfo.opType->setString("Distancetransform");
	// English readable name
	customInfo.opLabel->setString("Distance Transform");
	// Information of the author of the node
	customInfo.authorName->setString("Max Mainio Beidler");
	customInfo.authorEmail->setString("beidler.max@email.com");

	// This TOP takes one input
	customInfo.minInputs = 1;
	customInfo.maxInputs = 1;
}

DLLEXPORT
TD::TOP_CPlusPlusBase*
CreateTOPInstance(const TD::OP_NodeInfo* info, TD::TOP_Context* context)
{
	// Return a new instance of your class every time this is called.
	// It will be called once per TOP that is using the .dll
	return new DistanceTransformTOP(info, context);
}

DLLEXPORT
void
DestroyTOPInstance(TD::TOP_CPlusPlusBase* instance, TD::TOP_Context *context)
{
	// Delete the instance here, this will be called when
	// Touch is shutting down, when the TOP using that instance is deleted, or
	// if the TOP loads a different DLL
	delete (DistanceTransformTOP*)instance;
}

};


DistanceTransformTOP::DistanceTransformTOP(const TD::OP_NodeInfo*, TD::TOP_Context *context) :
	myFrame{ new cv::Mat() },
	myExecuteCount{0},
	myContext{context}
{
}

DistanceTransformTOP::~DistanceTransformTOP()
{
	delete myFrame;
}

void
DistanceTransformTOP::getGeneralInfo(TD::TOP_GeneralInfo* ginfo, const TD::OP_Inputs*, void*)
{
	ginfo->cookEveryFrame = false;
	ginfo->cookEveryFrameIfAsked = true;
	ginfo->inputSizeIndex = 0;
}

void
DistanceTransformTOP::execute(TD::TOP_Output* output, const TD::OP_Inputs* inputs, void*)
{
	using namespace cv;
	
	inputTopToMat(inputs);
	if (myFrame->empty())
		return;

	const TD::OP_TOPInput* top = inputs->getInputTOP(0);
	const TD::OP_PixelFormat inputFormat = top
		? top->textureDesc.pixelFormat
		: TD::OP_PixelFormat::Invalid;

	int distanceType = getType(myParms.evalDistancetype(inputs));
	int maskSize = getMask(myParms.evalMasksize(inputs));

	distanceTransform(*myFrame, *myFrame, distanceType, maskSize);
	
	bool donormalize = myParms.evalNormalize(inputs);

	if (donormalize)
		normalize(*myFrame, *myFrame, 0, 1.0, NORM_MINMAX);

	TD::OP_TextureDesc desc = TDPlugin::TOPOutput::resolvedDesc(
		output,
		static_cast<uint32_t>(myFrame->cols),
		static_cast<uint32_t>(myFrame->rows),
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
DistanceTransformTOP::setupParameters(TD::OP_ParameterManager* manager, void*)
{
	myParms.setup(manager);
}

void
DistanceTransformTOP::cvMatToOutput(TD::TOP_Output* out, TD::TOP_UploadInfo info) const
{
	size_t	height = info.textureDesc.height;
	size_t	width = info.textureDesc.width;
	cv::Mat outMat = *myFrame;

	if (outMat.cols != width || outMat.rows != height)
		cv::resize(outMat, outMat, cv::Size(width, height));

	cv::flip(outMat, outMat, 0);

	TDPlugin::TOPOutput::uploadMono32(
		myContext,
		out,
		static_cast<const float*>(static_cast<const void*>(outMat.data)),
		static_cast<uint32_t>(outMat.step),
		info.textureDesc,
		TD::TOP_FirstPixel::BottomLeft);
}

void 
DistanceTransformTOP::inputTopToMat(const TD::OP_Inputs* in)
{
	const TD::OP_TOPInput* top = in->getInputTOP(0);
	if (!top)
	{
		*myFrame = cv::Mat();
		return;
	}

	int chan = (int)myParms.evalChannel(in);

	TD::OP_TOPInputDownloadOptions opts;
	opts.verticalFlip = true;
	opts.pixelFormat = TD::OP_PixelFormat::RGBA8Fixed;

	TD::OP_SmartRef<TD::OP_TOPDownloadResult> downRes = top->downloadTexture(opts, nullptr);

	if (!downRes)
	{
		*myFrame = cv::Mat();
		return;
	}

	// Read the current download result directly.
	// This avoids being one cook behind when the input changes.
	uint8_t* pixel = static_cast<uint8_t*>(downRes->getData());

	if (!pixel)
	{
		*myFrame = cv::Mat();
		return;
	}

	int height = downRes->textureDesc.height;
	int width = downRes->textureDesc.width;

	*myFrame = cv::Mat(height, width, CV_8UC1);

	uint8_t* data = static_cast<uint8_t*>(myFrame->data);

	for (int i = 0; i < height; i += 1)
	{
		for (int j = 0; j < width; j += 1)
		{
			int pixelN = i * width + j;
			int index = 4 * pixelN + chan;
			data[pixelN] = pixel[index];
		}
	}
}
