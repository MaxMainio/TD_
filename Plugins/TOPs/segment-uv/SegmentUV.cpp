#include "SegmentUV.h"
#include "Parameters.h"
#include "../../shared/TOPOutputHelper.h"

#include <cassert>
#include <cstring>
#include <vector>
#include <algorithm>
#include <limits>
#include <random>
#include <cmath>

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

	customInfo.opType->setString("Segmentuv");
	customInfo.opLabel->setString("Segment UV");
	customInfo.authorName->setString("Max Mainio Beidler");
	customInfo.authorEmail->setString("beidler.max@email.com");

	customInfo.minInputs = 1;
	customInfo.maxInputs = 1;
}

DLLEXPORT
TD::TOP_CPlusPlusBase*
CreateTOPInstance(const TD::OP_NodeInfo* info, TD::TOP_Context* context)
{
	return new SegmentUV(info, context);
}

DLLEXPORT
void
DestroyTOPInstance(TD::TOP_CPlusPlusBase* instance, TD::TOP_Context* context)
{
	delete static_cast<SegmentUV*>(instance);
}

};

SegmentUV::SegmentUV(const TD::OP_NodeInfo*, TD::TOP_Context* context) :
	myInputFrame{ new cv::Mat() },
	myOutputFrame{ new cv::Mat() },
	myError{ "" },
	myContext{ context },
	myExecuteCount{ 0 }
{
}

SegmentUV::~SegmentUV()
{
	delete myInputFrame;
	delete myOutputFrame;
}

void
SegmentUV::getGeneralInfo(TD::TOP_GeneralInfo* ginfo, const TD::OP_Inputs*, void*)
{
	ginfo->cookEveryFrame = false;
	ginfo->cookEveryFrameIfAsked = true;
	ginfo->inputSizeIndex = 0;
}

void
SegmentUV::execute(TD::TOP_Output* output, const TD::OP_Inputs* inputs, void*)
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

	processSegments(inputs);

	if (myOutputFrame->empty())
		return;

	TD::OP_TextureDesc desc = TDPlugin::TOPOutput::resolvedDesc(
		output,
		static_cast<uint32_t>(myOutputFrame->cols),
		static_cast<uint32_t>(myOutputFrame->rows),
		TD::OP_PixelFormat::RG32Float,
		true,
		inputs,
		inputFormat
	);

	TD::TOP_UploadInfo info;
	info.textureDesc = desc;
	info.colorBufferIndex = 0;

	cvMatToOutput(output, info);
}

void
SegmentUV::setupParameters(TD::OP_ParameterManager* manager, void*)
{
	myParms.setup(manager);
}

void
SegmentUV::getErrorString(TD::OP_String* error, void*)
{
	error->setString(myError.c_str());
	myError.clear();
}

void
SegmentUV::inputTopToMat(const TD::OP_Inputs* inputs)
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

	// Clone so the frame remains valid after the download result goes out of scope.
	*myInputFrame = rgba.clone();
}

void
SegmentUV::processSegments(const TD::OP_Inputs* inputs)
{
	const int width = myInputFrame->cols;
	const int height = myInputFrame->rows;

	if (width <= 0 || height <= 0)
	{
		*myOutputFrame = cv::Mat();
		return;
	}

	const double alphaThreshold = myParms.evalAlphathreshold(inputs);
	const MethodMenuItems method = myParms.evalMethod(inputs);

	cv::Mat alphaMask(height, width, CV_8UC1, cv::Scalar(0));

	for (int y = 0; y < height; ++y)
	{
		const cv::Vec4f* inRow = myInputFrame->ptr<cv::Vec4f>(y);
		uint8_t* maskRow = alphaMask.ptr<uint8_t>(y);

		for (int x = 0; x < width; ++x)
		{
			float a = inRow[x][3];
			maskRow[x] = (a > alphaThreshold) ? 255 : 0;
		}
	}

	cv::Mat labels;
	cv::Mat stats;
	cv::Mat centroids;

	int numLabels = cv::connectedComponentsWithStats(
		alphaMask,
		labels,
		stats,
		centroids,
		8,
		CV_32S
	);

	*myOutputFrame = cv::Mat(height, width, CV_32FC4, cv::Scalar(0.0f, 0.0f, 0.0f, 0.0f));

	int seed = myParms.evalSeed(inputs);
	std::mt19937 rng(static_cast<uint32_t>(seed));
	std::uniform_real_distribution<float> dist(0.0f, 1.0f);

	auto uvFromPixel = [width, height](int x, int y) -> cv::Vec4f
	{
		float u = (width > 1) ? static_cast<float>(x) / static_cast<float>(width - 1) : 0.0f;
		float v = (height > 1) ? static_cast<float>(y) / static_cast<float>(height - 1) : 0.0f;
		return cv::Vec4f(u, v, 0.0f, 1.0f);
	};

	std::vector<cv::Vec4f> labelColors(numLabels, cv::Vec4f(0.0f, 0.0f, 0.0f, 0.0f));

	for (int label = 1; label < numLabels; ++label)
	{
		int left = stats.at<int>(label, cv::CC_STAT_LEFT);
		int top = stats.at<int>(label, cv::CC_STAT_TOP);
		int w = stats.at<int>(label, cv::CC_STAT_WIDTH);
		int h = stats.at<int>(label, cv::CC_STAT_HEIGHT);
		int area = stats.at<int>(label, cv::CC_STAT_AREA);

		if (area <= 0)
			continue;

		int px = left;
		int py = top;

		if (method == MethodMenuItems::Centroid)
		{
			double cx = centroids.at<double>(label, 0);
			double cy = centroids.at<double>(label, 1);

			px = static_cast<int>(std::round(cx));
			py = static_cast<int>(std::round(cy));

			px = std::clamp(px, 0, width - 1);
			py = std::clamp(py, 0, height - 1);
		}
		else if (method == MethodMenuItems::Boundingboxcenter)
		{
			px = left + w / 2;
			py = top + h / 2;

			px = std::clamp(px, 0, width - 1);
			py = std::clamp(py, 0, height - 1);
		}
		else if (method == MethodMenuItems::Medianpixelcoordinate)
		{
			std::vector<int> xs;
			std::vector<int> ys;
			xs.reserve(area);
			ys.reserve(area);

			for (int y = top; y < top + h; ++y)
			{
				const int* labelRow = labels.ptr<int>(y);

				for (int x = left; x < left + w; ++x)
				{
					if (labelRow[x] == label)
					{
						xs.push_back(x);
						ys.push_back(y);
					}
				}
			}

			if (!xs.empty())
			{
				size_t mid = xs.size() / 2;

				std::nth_element(xs.begin(), xs.begin() + mid, xs.end());
				std::nth_element(ys.begin(), ys.begin() + mid, ys.end());

				px = xs[mid];
				py = ys[mid];
			}
		}
		else if (method == MethodMenuItems::Closestsegmentpixeltocentroid)
		{
			double cx = centroids.at<double>(label, 0);
			double cy = centroids.at<double>(label, 1);

			double bestDist = std::numeric_limits<double>::max();

			for (int y = top; y < top + h; ++y)
			{
				const int* labelRow = labels.ptr<int>(y);

				for (int x = left; x < left + w; ++x)
				{
					if (labelRow[x] != label)
						continue;

					double dx = static_cast<double>(x) - cx;
					double dy = static_cast<double>(y) - cy;
					double d = dx * dx + dy * dy;

					if (d < bestDist)
					{
						bestDist = d;
						px = x;
						py = y;
					}
				}
			}
		}
		else if (method == MethodMenuItems::Random)
		{
			labelColors[label] = cv::Vec4f(dist(rng), dist(rng), dist(rng), 1.0f);
			continue;
		}

		labelColors[label] = uvFromPixel(px, py);
	}

	for (int y = 0; y < height; ++y)
	{
		const int* labelRow = labels.ptr<int>(y);
		cv::Vec4f* outRow = myOutputFrame->ptr<cv::Vec4f>(y);

		for (int x = 0; x < width; ++x)
		{
			int label = labelRow[x];

			if (label > 0)
				outRow[x] = labelColors[label];
		}
	}
}

void
SegmentUV::cvMatToOutput(TD::TOP_Output* output, TD::TOP_UploadInfo info) const
{
	cv::Mat outMat = *myOutputFrame;

	if (outMat.cols != static_cast<int>(info.textureDesc.width) ||
		outMat.rows != static_cast<int>(info.textureDesc.height))
	{
		cv::resize(
			outMat,
			outMat,
			cv::Size(info.textureDesc.width, info.textureDesc.height)
		);
	}

	cv::flip(outMat, outMat, 0);

	TDPlugin::TOPOutput::uploadRGBA32(
		myContext,
		output,
		static_cast<const float*>(static_cast<const void*>(outMat.data)),
		static_cast<uint32_t>(outMat.step),
		info.textureDesc,
		TD::TOP_FirstPixel::BottomLeft
	);
}
