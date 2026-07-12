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

#include "BasicFilterTOP.h"
#include "Parameters.h"

#include "ThreadManager.h"
#include "FilterWork.h"
#include "../../shared/TOPOutputHelper.h"

#include <cassert>
#include <vector>
#include <cstdlib>
// These functions are basic C function, which the DLL loader can find
// much easier than finding a C++ Class.
// The DLLEXPORT prefix is needed so the compile exports these functions from the .dll
// you are creating
extern "C"
{

DLLEXPORT
void
FillTOPPluginInfo(TOP_PluginInfo *info)
{
	// This must always be set to this constant.
	if (!info->setAPIVersion(TOPCPlusPlusAPIVersion))
	{
		return;
	}

	// Change this to change the executeMode behavior of this plugin.
	info->executeMode = TOP_ExecuteMode::CPUMem;

	// For more information on OP_CustomOPInfo see CPlusPlus_Common.h
	OP_CustomOPInfo& customInfo = info->customOPInfo;

	// Unique name of the node which starts with an upper case letter, followed by lower case letters or numbers
	customInfo.opType->setString("Basicfilter");
	// English readable name
	customInfo.opLabel->setString("Filter");
	// Information of the author of the node
	customInfo.authorName->setString("Max Mainio Beidler");
	customInfo.authorEmail->setString("beidler.max@email.com");

	// This TOP takes one input
	customInfo.minInputs = 1;
	customInfo.maxInputs = 1;
}

DLLEXPORT
TOP_CPlusPlusBase*
CreateTOPInstance(const OP_NodeInfo* info, TOP_Context* context)
{
	// Return a new instance of your class every time this is called.
	// It will be called once per TOP that is using the .dll
	return new BasicFilterTOP(info, context);
}

DLLEXPORT
void
DestroyTOPInstance(TOP_CPlusPlusBase* instance, TOP_Context *context)
{
	// Delete the instance here, this will be called when
	// Touch is shutting down, when the TOP using that instance is deleted, or
	// if the TOP loads a different DLL
	delete (BasicFilterTOP*)instance;
}

};

BasicFilterTOP::BasicFilterTOP(const OP_NodeInfo* info, TOP_Context* context) :
	myThreadManagers{}, myThreadQueue{}, myExecuteCount{ 0 }, myMultiThreaded{ false },
	myContext{ context},
	myPrevDownRes{}
{
}

BasicFilterTOP::~BasicFilterTOP()
{
}

void
BasicFilterTOP::getGeneralInfo(TOP_GeneralInfo* ginfo, const TD::OP_Inputs*, void*)
{
	ginfo->cookEveryFrameIfAsked = false;
	ginfo->inputSizeIndex = 0;
}



void
BasicFilterTOP::execute(TOP_Output* output, const TD::OP_Inputs* inputs, void* reserved)
{
	myExecuteCount++;
	const OP_TOPInput*	top = inputs->getInputTOP(0);

	if (!top)
		return;

	OP_TOPInputDownloadOptions	opts;
	opts.pixelFormat = TD::OP_PixelFormat::BGRA8Fixed;


	OP_SmartRef<OP_TOPDownloadResult> downRes = top->downloadTexture(opts,nullptr);

	if (!downRes)
		return;

	int inHeight = downRes->textureDesc.height;
	int inWidth = downRes->textureDesc.width;

	TD::OP_TextureDesc outputDesc = TDPlugin::TOPOutput::resolvedDesc(
		output,
		static_cast<uint32_t>(inWidth),
		static_cast<uint32_t>(inHeight),
		top->textureDesc.pixelFormat,
		true,
		inputs,
		top->textureDesc.pixelFormat);

	if (outputDesc.width == 0 || outputDesc.height == 0)
		return;

	bool doDither = myParms.evalDither(inputs);
	int bitsPerColor = myParms.evalBitspercolor(inputs);
	myPrevDownRes = std::move(downRes);
	if (myPrevDownRes)
	{
		if (myMultiThreaded)
		{
			if (myThreadQueue.empty())
			{
				ThreadManager* threadForWork = myThreadManagers.at(0);
				threadForWork->sync(doDither, bitsPerColor, inWidth, inHeight, outputDesc, myPrevDownRes, myContext);
				myThreadQueue.push(threadForWork);
			}
			else if (myThreadQueue.front()->getStatus() == ThreadStatus::Done)
			{
				ThreadManager* threadForWork = myThreadQueue.front();
				myThreadQueue.pop();
				OP_SmartRef<TOP_Buffer> outBuffer;
				TOP_UploadInfo info;
				threadForWork->popOutBuffer(outBuffer, info);
				if (outBuffer)
				{
					TDPlugin::TOPOutput::uploadBGRA8(
						myContext,
						output,
						static_cast<const uint8_t*>(outBuffer->data),
						info.textureDesc.width * 4,
						info.textureDesc,
						info.firstPixel);
				}

				threadForWork->sync(doDither, bitsPerColor, inWidth, inHeight, outputDesc, myPrevDownRes, myContext);
				myThreadQueue.push(threadForWork);
			}
			else
			{
				for (ThreadManager* tm : myThreadManagers)
				{
					if (tm->getStatus() == ThreadStatus::Waiting)
					{
						tm->sync(doDither, bitsPerColor, inWidth, inHeight, outputDesc, myPrevDownRes, myContext);
						myThreadQueue.push(tm);
						break;
					}
				}
			}
		}
		else
		{

			if (!myThreadQueue.empty())
				switchToSingleThreaded();


			uint32_t* inBuffer = (uint32_t*)myPrevDownRes->getData();
			std::vector<uint32_t> outBuffer(
				static_cast<size_t>(outputDesc.width) *
				static_cast<size_t>(outputDesc.height));

			Filter::doFilterWork(
				inBuffer,
				inWidth,
				inHeight,
				outBuffer.data(),
				static_cast<int>(outputDesc.width),
				static_cast<int>(outputDesc.height),
				doDither,
				bitsPerColor
			);

			TDPlugin::TOPOutput::uploadBGRA8(
				myContext,
				output,
				reinterpret_cast<const uint8_t*>(outBuffer.data()),
				outputDesc.width * 4,
				outputDesc,
				TD::TOP_FirstPixel::BottomLeft);
		}
	}
	// myPrevDownRes = std::move(downRes);

	bool threaded = myParms.evalMultithreaded(inputs);

	if (threaded && !myMultiThreaded)
		switchToMultiThreaded();

	if (!threaded && myMultiThreaded)
		switchToSingleThreaded();
}

void 
BasicFilterTOP::setupParameters(OP_ParameterManager* manager, void* reserved)
{
	myParms.setup(manager);
}

void 
BasicFilterTOP::switchToSingleThreaded()
{
	for (int i = 0; i < NumCPUPixelDatas; ++i)
	{
		delete myThreadManagers.at(i);
		myThreadManagers.at(i) = nullptr;
	}


	while (!myThreadQueue.empty()) 
	{
		myThreadQueue.pop();
	}
	myMultiThreaded = false;
}

void 
BasicFilterTOP::switchToMultiThreaded()
{
	// This delays the output by 3 frames
	// myExecuteCount = -3;

	for (int i = 0; i < NumCPUPixelDatas; ++i)
	{
		myThreadManagers.at(i) = new ThreadManager();
	}

	myMultiThreaded = true;
}
