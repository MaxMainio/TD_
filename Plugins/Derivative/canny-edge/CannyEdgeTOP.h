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

#ifndef __CannyEdgeTOP__
#define __CannyEdgeTOP__

#include "TOP_CPlusPlusBase.h"
#include "Parameters.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <string>

class CannyEdgeTOP : public TD::TOP_CPlusPlusBase
{
public:
	CannyEdgeTOP(const TD::OP_NodeInfo* info, TD::TOP_Context* context);
	virtual ~CannyEdgeTOP();

	virtual void getGeneralInfo(TD::TOP_GeneralInfo*, const TD::OP_Inputs*, void* reserved) override;
	virtual void execute(TD::TOP_Output*, const TD::OP_Inputs*, void* reserved) override;
	virtual void setupParameters(TD::OP_ParameterManager*, void* reserved) override;
	virtual void getErrorString(TD::OP_String*, void* reserved) override;

private:
	void inputTopToMat(const TD::OP_Inputs* inputs);
	void cvMatToOutput(TD::TOP_Output* output, TD::TOP_UploadInfo info) const;

	cv::Mat*			myFrame;
	cv::Mat*			myOutputFrame;
	std::string			myError;
	TD::TOP_Context*	myContext;
	Parameters			myParms;
	int32_t				myExecuteCount;
};

#endif
