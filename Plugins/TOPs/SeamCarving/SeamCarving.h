#ifndef __SeamCarving__
#define __SeamCarving__

#include "TOP_CPlusPlusBase.h"
#include "Parameters.h"

#include <opencv2/core.hpp>
#include <string>
#include <vector>

class SeamCarving : public TD::TOP_CPlusPlusBase
{
public:
	SeamCarving(const TD::OP_NodeInfo* info, TD::TOP_Context* context);
	virtual ~SeamCarving();

	virtual void getGeneralInfo(TD::TOP_GeneralInfo*, const TD::OP_Inputs*, void* reserved) override;
	virtual void execute(TD::TOP_Output*, const TD::OP_Inputs*, void* reserved) override;
	virtual void setupParameters(TD::OP_ParameterManager*, void* reserved) override;
	virtual void getErrorString(TD::OP_String*, void* reserved) override;

private:
	void inputImageToMat(const TD::OP_Inputs* inputs);
	bool inputEnergyToMat(const TD::OP_Inputs* inputs, int targetWidth, int targetHeight);

	void computeInternalEnergy(const cv::Mat& image, cv::Mat& energy) const;
	void computeCumulativeCost(const cv::Mat& energy, cv::Mat& cost) const;
	void traceVerticalSeam(const cv::Mat& cost, std::vector<int>& seam) const;

	void removeVerticalSeamRGBA(const cv::Mat& input, const std::vector<int>& seam, cv::Mat& output) const;
	void removeVerticalSeamMono(const cv::Mat& input, const std::vector<int>& seam, cv::Mat& output) const;

	void collectInsertionSeams(
		const cv::Mat& image,
		const cv::Mat& externalEnergy,
		bool hasExternalEnergy,
		CostUpdateMenuItems costUpdate,
		int insertCount,
		std::vector<std::vector<int>>& seams
	) const;

	cv::Vec4f insertedRGBAPixel(const cv::Mat& input, int x, int y) const;
	void insertVerticalSeamsRGBA(
		const cv::Mat& input,
		const std::vector<std::vector<int>>& seams,
		cv::Mat& output
	) const;

	void resizeMonoNearest(const cv::Mat& input, int width, int height, cv::Mat& output) const;

	void carveImage(const TD::OP_Inputs* inputs);
	void cvMatToOutput(TD::TOP_Output* output, TD::TOP_UploadInfo info) const;

	cv::Mat*			myInputFrame;
	cv::Mat*			myEnergyFrame;
	cv::Mat*			myOutputFrame;

	std::string			myError;
	TD::TOP_Context*	myContext;
	Parameters			myParms;
	int32_t				myExecuteCount;
};

#endif