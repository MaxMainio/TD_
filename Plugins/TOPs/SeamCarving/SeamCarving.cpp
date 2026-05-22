#include "SeamCarving.h"
#include "Parameters.h"

#include <cassert>
#include <cstring>
#include <algorithm>
#include <limits>
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

	customInfo.opType->setString("Seamcarving");
	customInfo.opLabel->setString("Seam Carving");
	customInfo.authorName->setString("Max Mainio Beidler");
	customInfo.authorEmail->setString("beidler.max@email.com");

	customInfo.minInputs = 1;
	customInfo.maxInputs = 2;
}

DLLEXPORT
TD::TOP_CPlusPlusBase*
CreateTOPInstance(const TD::OP_NodeInfo* info, TD::TOP_Context* context)
{
	return new SeamCarving(info, context);
}

DLLEXPORT
void
DestroyTOPInstance(TD::TOP_CPlusPlusBase* instance, TD::TOP_Context* context)
{
	delete static_cast<SeamCarving*>(instance);
}

};

SeamCarving::SeamCarving(const TD::OP_NodeInfo*, TD::TOP_Context* context) :
	myInputFrame{ new cv::Mat() },
	myEnergyFrame{ new cv::Mat() },
	myOutputFrame{ new cv::Mat() },
	myError{ "" },
	myContext{ context },
	myExecuteCount{ 0 }
{
}

SeamCarving::~SeamCarving()
{
	delete myInputFrame;
	delete myEnergyFrame;
	delete myOutputFrame;
}

void
SeamCarving::getGeneralInfo(TD::TOP_GeneralInfo* ginfo, const TD::OP_Inputs*, void*)
{
	ginfo->cookEveryFrame = false;
	ginfo->cookEveryFrameIfAsked = true;
	ginfo->inputSizeIndex = 0;
}

void
SeamCarving::execute(TD::TOP_Output* output, const TD::OP_Inputs* inputs, void*)
{
	myError = "";
	myExecuteCount++;

	inputImageToMat(inputs);

	if (myInputFrame->empty())
		return;

	carveImage(inputs);

	if (myOutputFrame->empty())
		return;

	TD::TOP_UploadInfo info;
	info.textureDesc.width = myOutputFrame->cols;
	info.textureDesc.height = myOutputFrame->rows;
	info.textureDesc.texDim = TD::OP_TexDim::e2D;
	info.textureDesc.pixelFormat = TD::OP_PixelFormat::RGBA32Float;
	info.colorBufferIndex = 0;

	cvMatToOutput(output, info);
}

void
SeamCarving::setupParameters(TD::OP_ParameterManager* manager, void*)
{
	myParms.setup(manager);
}

void
SeamCarving::getErrorString(TD::OP_String* error, void*)
{
	error->setString(myError.c_str());
	myError.clear();
}

void
SeamCarving::inputImageToMat(const TD::OP_Inputs* inputs)
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

bool
SeamCarving::inputEnergyToMat(const TD::OP_Inputs* inputs, int targetWidth, int targetHeight)
{
	const TD::OP_TOPInput* top = inputs->getInputTOP(1);
	if (!top)
	{
		*myEnergyFrame = cv::Mat();
		return false;
	}

	TD::OP_TOPInputDownloadOptions opts;
	opts.verticalFlip = true;
	opts.pixelFormat = TD::OP_PixelFormat::Mono32Float;

	TD::OP_SmartRef<TD::OP_TOPDownloadResult> downRes = top->downloadTexture(opts, nullptr);

	if (!downRes)
	{
		*myEnergyFrame = cv::Mat();
		return false;
	}

	float* pixel = static_cast<float*>(downRes->getData());

	if (!pixel)
	{
		*myEnergyFrame = cv::Mat();
		return false;
	}

	int width = downRes->textureDesc.width;
	int height = downRes->textureDesc.height;

	cv::Mat mono(height, width, CV_32FC1, pixel);
	cv::Mat energy = mono.clone();

	if (width != targetWidth || height != targetHeight)
	{
		cv::Mat resized;
		resizeMonoNearest(energy, targetWidth, targetHeight, resized);
		*myEnergyFrame = resized;
	}
	else
	{
		*myEnergyFrame = energy;
	}

	return true;
}

void
SeamCarving::computeInternalEnergy(const cv::Mat& image, cv::Mat& energy) const
{
	const int width = image.cols;
	const int height = image.rows;

	energy = cv::Mat(height, width, CV_32FC1, cv::Scalar(0.0f));

	auto lumaAt = [&image, width, height](int x, int y) -> float
	{
		x = std::clamp(x, 0, width - 1);
		y = std::clamp(y, 0, height - 1);

		const cv::Vec4f& px = image.at<cv::Vec4f>(y, x);

		// Rec. 709-ish luminance weights.
		return px[0] * 0.2126f + px[1] * 0.7152f + px[2] * 0.0722f;
	};

	for (int y = 0; y < height; ++y)
	{
		float* energyRow = energy.ptr<float>(y);

		for (int x = 0; x < width; ++x)
		{
			float left = lumaAt(x - 1, y);
			float right = lumaAt(x + 1, y);
			float up = lumaAt(x, y - 1);
			float down = lumaAt(x, y + 1);

			float dx = std::abs(right - left);
			float dy = std::abs(down - up);

			energyRow[x] = dx + dy;
		}
	}
}

void
SeamCarving::computeCumulativeCost(const cv::Mat& energy, cv::Mat& cost) const
{
	const int width = energy.cols;
	const int height = energy.rows;

	cost = cv::Mat(height, width, CV_32FC1, cv::Scalar(0.0f));

	// Bottom row initializes directly from energy.
	const int bottomY = height - 1;

	const float* bottomEnergy = energy.ptr<float>(bottomY);
	float* bottomCost = cost.ptr<float>(bottomY);

	for (int x = 0; x < width; ++x)
	{
		bottomCost[x] = bottomEnergy[x];
	}

	// Accumulate minimum cost from bottom to top.
	for (int y = height - 2; y >= 0; --y)
	{
		const float* energyRow = energy.ptr<float>(y);
		const float* belowRow = cost.ptr<float>(y + 1);
		float* costRow = cost.ptr<float>(y);

		for (int x = 0; x < width; ++x)
		{
			float best = belowRow[x];

			if (x > 0)
				best = std::min(best, belowRow[x - 1]);

			if (x < width - 1)
				best = std::min(best, belowRow[x + 1]);

			costRow[x] = energyRow[x] + best;
		}
	}
}

void
SeamCarving::traceVerticalSeam(const cv::Mat& cost, std::vector<int>& seam) const
{
	const int width = cost.cols;
	const int height = cost.rows;

	seam.assign(height, 0);

	if (width <= 0 || height <= 0)
		return;

	// Start from the minimum-cost pixel in the top row.
	const float* topRow = cost.ptr<float>(0);

	int x = 0;
	float best = topRow[0];

	for (int i = 1; i < width; ++i)
	{
		if (topRow[i] < best)
		{
			best = topRow[i];
			x = i;
		}
	}

	seam[0] = x;

	// Walk downward, choosing the cheapest connected pixel in the next row.
	for (int y = 1; y < height; ++y)
	{
		const float* row = cost.ptr<float>(y);

		int bestX = x;
		float bestCost = row[x];

		if (x > 0 && row[x - 1] < bestCost)
		{
			bestCost = row[x - 1];
			bestX = x - 1;
		}

		if (x < width - 1 && row[x + 1] < bestCost)
		{
			bestCost = row[x + 1];
			bestX = x + 1;
		}

		x = bestX;
		seam[y] = x;
	}
}

void
SeamCarving::removeVerticalSeamRGBA(const cv::Mat& input, const std::vector<int>& seam, cv::Mat& output) const
{
	const int width = input.cols;
	const int height = input.rows;

	if (width <= 1 || height <= 0)
	{
		output = input.clone();
		return;
	}

	output = cv::Mat(height, width - 1, CV_32FC4);

	for (int y = 0; y < height; ++y)
	{
		const cv::Vec4f* inRow = input.ptr<cv::Vec4f>(y);
		cv::Vec4f* outRow = output.ptr<cv::Vec4f>(y);

		int seamX = std::clamp(seam[y], 0, width - 1);
		int outX = 0;

		for (int x = 0; x < width; ++x)
		{
			if (x == seamX)
				continue;

			outRow[outX] = inRow[x];
			++outX;
		}
	}
}

void
SeamCarving::removeVerticalSeamMono(const cv::Mat& input, const std::vector<int>& seam, cv::Mat& output) const
{
	const int width = input.cols;
	const int height = input.rows;

	if (width <= 1 || height <= 0)
	{
		output = input.clone();
		return;
	}

	output = cv::Mat(height, width - 1, CV_32FC1);

	for (int y = 0; y < height; ++y)
	{
		const float* inRow = input.ptr<float>(y);
		float* outRow = output.ptr<float>(y);

		int seamX = std::clamp(seam[y], 0, width - 1);
		int outX = 0;

		for (int x = 0; x < width; ++x)
		{
			if (x == seamX)
				continue;

			outRow[outX] = inRow[x];
			++outX;
		}
	}
}

void
SeamCarving::collectInsertionSeams(
	const cv::Mat& image,
	const cv::Mat& externalEnergy,
	bool hasExternalEnergy,
	CostUpdateMenuItems costUpdate,
	int insertCount,
	std::vector<std::vector<int>>& seams
) const
{
	const int originalWidth = image.cols;
	const int height = image.rows;

	seams.clear();

	if (originalWidth <= 0 || height <= 0 || insertCount <= 0)
		return;

	insertCount = std::clamp(insertCount, 0, originalWidth - 1);

	// Temporary image is progressively carved to find multiple distinct seams.
	cv::Mat tempImage = image.clone();

	cv::Mat tempEnergy;
	if (hasExternalEnergy)
		tempEnergy = externalEnergy.clone();
	else
		computeInternalEnergy(tempImage, tempEnergy);

	cv::Mat tempCost;

	if (costUpdate == CostUpdateMenuItems::Once)
		computeCumulativeCost(tempEnergy, tempCost);

	// indexMap[y][x] tells us which original x-coordinate the temporary x-coordinate represents.
	std::vector<std::vector<int>> indexMap(height);

	for (int y = 0; y < height; ++y)
	{
		indexMap[y].resize(originalWidth);

		for (int x = 0; x < originalWidth; ++x)
			indexMap[y][x] = x;
	}

	for (int i = 0; i < insertCount; ++i)
	{
		if (tempImage.cols <= 1)
			break;

		if (costUpdate == CostUpdateMenuItems::Everyseam)
		{
			if (!hasExternalEnergy)
				computeInternalEnergy(tempImage, tempEnergy);

			computeCumulativeCost(tempEnergy, tempCost);
		}

		if (tempCost.empty())
			break;

		std::vector<int> tempSeam;
		traceVerticalSeam(tempCost, tempSeam);

		if (tempSeam.size() != static_cast<size_t>(height))
			break;

		std::vector<int> originalSeam(height, 0);

		for (int y = 0; y < height; ++y)
		{
			int tempX = std::clamp(tempSeam[y], 0, static_cast<int>(indexMap[y].size()) - 1);
			originalSeam[y] = indexMap[y][tempX];
		}

		seams.push_back(originalSeam);

		// Remove the seam from the temporary image.
		cv::Mat carvedImage;
		removeVerticalSeamRGBA(tempImage, tempSeam, carvedImage);
		tempImage = carvedImage;

		// Remove the seam from the temporary energy map.
		if (!tempEnergy.empty() && tempEnergy.cols > 1)
		{
			cv::Mat carvedEnergy;
			removeVerticalSeamMono(tempEnergy, tempSeam, carvedEnergy);
			tempEnergy = carvedEnergy;
		}

		// If using "Once", also shrink the original cumulative cost map.
		if (costUpdate == CostUpdateMenuItems::Once && !tempCost.empty() && tempCost.cols > 1)
		{
			cv::Mat carvedCost;
			removeVerticalSeamMono(tempCost, tempSeam, carvedCost);
			tempCost = carvedCost;
		}

		// Remove the same seam from the index map.
		for (int y = 0; y < height; ++y)
		{
			int tempX = std::clamp(tempSeam[y], 0, static_cast<int>(indexMap[y].size()) - 1);
			indexMap[y].erase(indexMap[y].begin() + tempX);
		}
	}
}

cv::Vec4f
SeamCarving::insertedRGBAPixel(const cv::Mat& input, int x, int y) const
{
	const int width = input.cols;

	const cv::Vec4f* row = input.ptr<cv::Vec4f>(y);

	if (width <= 1)
		return row[0];

	if (x <= 0)
		return 0.5f * (row[0] + row[1]);

	if (x >= width - 1)
		return 0.5f * (row[width - 2] + row[width - 1]);

	return 0.5f * (row[x - 1] + row[x + 1]);
}

void
SeamCarving::insertVerticalSeamsRGBA(
	const cv::Mat& input,
	const std::vector<std::vector<int>>& seams,
	cv::Mat& output
) const
{
	const int width = input.cols;
	const int height = input.rows;
	const int insertCount = static_cast<int>(seams.size());

	if (width <= 0 || height <= 0 || insertCount <= 0)
	{
		output = input.clone();
		return;
	}

	output = cv::Mat(height, width + insertCount, CV_32FC4);

	for (int y = 0; y < height; ++y)
	{
		std::vector<int> rowSeams;
		rowSeams.reserve(insertCount);

		for (const std::vector<int>& seam : seams)
		{
			if (y < static_cast<int>(seam.size()))
				rowSeams.push_back(std::clamp(seam[y], 0, width - 1));
		}

		std::sort(rowSeams.begin(), rowSeams.end());

		const cv::Vec4f* inRow = input.ptr<cv::Vec4f>(y);
		cv::Vec4f* outRow = output.ptr<cv::Vec4f>(y);

		int outX = 0;
		int seamIndex = 0;

		for (int x = 0; x < width; ++x)
		{
			outRow[outX] = inRow[x];
			++outX;

			while (
				seamIndex < static_cast<int>(rowSeams.size()) &&
				rowSeams[seamIndex] == x
			)
			{
				outRow[outX] = insertedRGBAPixel(input, x, y);
				++outX;
				++seamIndex;
			}
		}
	}
}

void
SeamCarving::resizeMonoNearest(const cv::Mat& input, int width, int height, cv::Mat& output) const
{
	if (input.empty() || width <= 0 || height <= 0)
	{
		output = cv::Mat();
		return;
	}

	output = cv::Mat(height, width, CV_32FC1);

	const int inWidth = input.cols;
	const int inHeight = input.rows;

	for (int y = 0; y < height; ++y)
	{
		float* outRow = output.ptr<float>(y);

		int srcY = 0;
		if (height > 1)
			srcY = static_cast<int>(std::round(static_cast<float>(y) * static_cast<float>(inHeight - 1) / static_cast<float>(height - 1)));

		srcY = std::clamp(srcY, 0, inHeight - 1);

		const float* inRow = input.ptr<float>(srcY);

		for (int x = 0; x < width; ++x)
		{
			int srcX = 0;
			if (width > 1)
				srcX = static_cast<int>(std::round(static_cast<float>(x) * static_cast<float>(inWidth - 1) / static_cast<float>(width - 1)));

			srcX = std::clamp(srcX, 0, inWidth - 1);
			outRow[x] = inRow[srcX];
		}
	}
}

void
SeamCarving::carveImage(const TD::OP_Inputs* inputs)
{
	cv::Mat currentImage = myInputFrame->clone();

	const int inputWidth = currentImage.cols;
	const int inputHeight = currentImage.rows;

	if (inputWidth <= 0 || inputHeight <= 0)
	{
		*myOutputFrame = cv::Mat();
		return;
	}

	int carveWidth = myParms.evalCarvewidth(inputs);
	const CostUpdateMenuItems costUpdate = myParms.evalCostupdate(inputs);

	bool hasExternalEnergy = inputEnergyToMat(inputs, inputWidth, inputHeight);

	cv::Mat currentEnergy;

	if (hasExternalEnergy)
		currentEnergy = myEnergyFrame->clone();
	else
		computeInternalEnergy(currentImage, currentEnergy);

	if (carveWidth == 0)
	{
		*myOutputFrame = currentImage;
		return;
	}

	// Positive values remove seams.
	if (carveWidth > 0)
	{
		int removeCount = std::clamp(carveWidth, 0, inputWidth - 1);

		cv::Mat currentCost;

		if (costUpdate == CostUpdateMenuItems::Once)
			computeCumulativeCost(currentEnergy, currentCost);

		for (int i = 0; i < removeCount; ++i)
		{
			if (currentImage.cols <= 1)
				break;

			if (costUpdate == CostUpdateMenuItems::Everyseam)
			{
				if (!hasExternalEnergy)
					computeInternalEnergy(currentImage, currentEnergy);

				computeCumulativeCost(currentEnergy, currentCost);
			}

			if (currentCost.empty())
				break;

			std::vector<int> seam;
			traceVerticalSeam(currentCost, seam);

			cv::Mat carvedImage;
			removeVerticalSeamRGBA(currentImage, seam, carvedImage);
			currentImage = carvedImage;

			if (!currentEnergy.empty() && currentEnergy.cols > 1)
			{
				cv::Mat carvedEnergy;
				removeVerticalSeamMono(currentEnergy, seam, carvedEnergy);
				currentEnergy = carvedEnergy;
			}

			if (costUpdate == CostUpdateMenuItems::Once && !currentCost.empty() && currentCost.cols > 1)
			{
				cv::Mat carvedCost;
				removeVerticalSeamMono(currentCost, seam, carvedCost);
				currentCost = carvedCost;
			}
		}

		*myOutputFrame = currentImage;
		return;
	}

	// Negative values insert seams.
	int insertCount = std::clamp(-carveWidth, 0, 256);

	std::vector<std::vector<int>> seamsToInsert;

	collectInsertionSeams(
		currentImage,
		currentEnergy,
		hasExternalEnergy,
		costUpdate,
		insertCount,
		seamsToInsert
	);

	cv::Mat expandedImage;
	insertVerticalSeamsRGBA(currentImage, seamsToInsert, expandedImage);

	*myOutputFrame = expandedImage;
}

void
SeamCarving::cvMatToOutput(TD::TOP_Output* output, TD::TOP_UploadInfo info) const
{
	size_t width = info.textureDesc.width;
	size_t height = info.textureDesc.height;
	size_t imgSize = width * height * 4 * sizeof(float);

	TD::OP_SmartRef<TD::TOP_Buffer> buf = myContext->createOutputBuffer(imgSize, TD::TOP_BufferFlags::None, nullptr);
	float* outPixel = static_cast<float*>(buf->data);

	cv::Mat outMat = *myOutputFrame;

	cv::flip(outMat, outMat, 0);

	std::memcpy(outPixel, outMat.data, imgSize);

	output->uploadBuffer(&buf, info, nullptr);
}