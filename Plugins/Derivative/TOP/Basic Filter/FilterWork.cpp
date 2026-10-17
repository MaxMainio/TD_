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

#include "FilterWork.h"

namespace
{
	void
		diffuseError(uint32_t& pixel, uint32_t error, double coeff)
	{
		uint8_t*	pxChan = reinterpret_cast<uint8_t*>(&pixel);
		uint8_t*	eChan = reinterpret_cast<uint8_t*>(&error);

		for (int i = 0; i < 3; ++i)
		{
			double	temp = pxChan[i] + coeff * eChan[i];
			if (temp > UINT8_MAX)
				pxChan[i] = UINT8_MAX;
			else
				pxChan[i] = static_cast<uint8_t>(temp);
		}
	}

	// This returns a buffer of pixels which the user should delete[] when its done using it
	uint32_t*
		resizeImage(uint32_t* inPixel, int inWidth, int inHeight, int outWidth, int outHeight)
	{
		uint32_t* ret = new uint32_t[outWidth * outHeight];
		double scaleX = inWidth / static_cast<double>(outWidth);
		double scaleY = inHeight / static_cast<double>(outHeight);

		for (int y = 0; y < outHeight; ++y)
		{
			for (int x = 0; x < outWidth; ++x)
			{
				int inY = static_cast<int>(y * scaleY);
				int inX = static_cast<int>(x * scaleX);
				ret[y * outWidth + x] = inPixel[inY * inWidth + inX];
			}
		}
		return ret;
	}

	uint32_t
		closestPaletteColor(uint32_t color, int colorBits)
	{
		uint8_t*	channels = reinterpret_cast<uint8_t*>(&color);
		uint8_t		outChannels[4];
		const int	shift = 8 - colorBits;

		for (int i = 0; i < 3; ++i)
		{
			outChannels[i] = (channels[i] >> shift) << shift;
		}
		outChannels[3] = channels[3];

		return *reinterpret_cast<uint32_t*>(outChannels);
	}

	void
		doDithering(uint32_t* inPixel, uint32_t* outPixel, int w, int h, int colorBits)
	{
		for (int i = 0; i < h; ++i)
		{
			for (int j = 0; j < w; ++j)
			{
				uint32_t	oldPx = inPixel[i * w + j];
				uint32_t	newPx = closestPaletteColor(oldPx, colorBits);
				uint32_t	error = oldPx - newPx;
				outPixel[i * w + j] = newPx;

				if (j != w - 1)
					diffuseError(inPixel[i * w + j + 1], error, 7.0 / 16);
				if (j != 0 && i != h - 1)
					diffuseError(inPixel[(i + 1) * w + j - 1], error, 3.0 / 16);
				if (i != h - 1)
					diffuseError(inPixel[(i + 1) * w + j], error, 5.0 / 16);
				if (j != w - 1 && i != h - 1)
					diffuseError(inPixel[(i + 1) * w + j + 1], error, 1.0 / 16);
			}
		}
	}

	void
		limitColors(uint32_t* inPixel, uint32_t* outPixel, int w, int h, int colorBits)
	{
		for (int i = 0; i < w * h; ++i)
		{
			outPixel[i] = closestPaletteColor(inPixel[i], colorBits);
		}
	}
}


void Filter::doFilterWork(uint32_t* inBuffer, int inWidth, int inHeight, uint32_t* outBuffer, int outWidth, int outHeight, bool doDither, int bitsPerColor)
{
	bool	needsResize = inHeight != outHeight || inWidth != outWidth;

	if (needsResize)
	{
		inBuffer = resizeImage(inBuffer, inWidth, inHeight, outWidth, outHeight);
	}
	
	if (doDither)
		doDithering(inBuffer, outBuffer, outWidth, outHeight, bitsPerColor);
	else
		limitColors(inBuffer, outBuffer, outWidth, outHeight, bitsPerColor);

	if (needsResize)
	{
		delete[] inBuffer;
	}
}
