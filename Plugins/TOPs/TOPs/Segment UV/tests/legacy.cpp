// Image algorithm frozen from SegmentUV.cpp at 17f2e967. Only the host plumbing
// was replaced by arguments/return value, for exact output and timing comparisons.
#include "SegmentEngine.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <limits>
#include <random>
#include <cmath>

cv::Mat legacyProcess(const cv::Mat& input, double alphaThreshold, MethodMenuItems method, int seed)
{
	const int width = input.cols;
	const int height = input.rows;

	if (width <= 0 || height <= 0)
	{
		return {};
	}


	cv::Mat alphaMask(height, width, CV_8UC1, cv::Scalar(0));

	for (int y = 0; y < height; ++y)
	{
		const cv::Vec4f* inRow = input.ptr<cv::Vec4f>(y);
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

	cv::Mat result(height, width, CV_32FC4, cv::Scalar(0.0f, 0.0f, 0.0f, 0.0f));

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
		cv::Vec4f* outRow = result.ptr<cv::Vec4f>(y);

		for (int x = 0; x < width; ++x)
		{
			int label = labelRow[x];

			if (label > 0)
				outRow[x] = labelColors[label];
		}
	}
	return result;
}
