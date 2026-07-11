#ifndef __SegmentUV__
#define __SegmentUV__

#include "TOP_CPlusPlusBase.h"
#include "Parameters.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <string>

class SegmentUV : public TD::TOP_CPlusPlusBase
{
public:
	SegmentUV(const TD::OP_NodeInfo* info, TD::TOP_Context* context);
	virtual ~SegmentUV();

	virtual void getGeneralInfo(TD::TOP_GeneralInfo*, const TD::OP_Inputs*, void* reserved) override;
	virtual void execute(TD::TOP_Output*, const TD::OP_Inputs*, void* reserved) override;
	virtual void setupParameters(TD::OP_ParameterManager*, void* reserved) override;
	virtual void getErrorString(TD::OP_String*, void* reserved) override;

private:
	void inputTopToMat(const TD::OP_Inputs* inputs);
	void processSegments(const TD::OP_Inputs* inputs);
	void cvMatToOutput(TD::TOP_Output* output, TD::TOP_UploadInfo info) const;

	cv::Mat*			myInputFrame;
	cv::Mat*			myOutputFrame;
	std::string			myError;
	TD::TOP_Context*	myContext;
	Parameters			myParms;
	int32_t				myExecuteCount;
};

#endif