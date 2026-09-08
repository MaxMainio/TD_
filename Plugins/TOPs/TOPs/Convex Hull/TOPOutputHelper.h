#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace TDPlugin
{
namespace TOPOutput
{
inline bool
isSupportedOutputFormat(TD::OP_PixelFormat format)
{
	switch (format)
	{
		case TD::OP_PixelFormat::BGRA8Fixed:
		case TD::OP_PixelFormat::RGBA8Fixed:
		case TD::OP_PixelFormat::RGBA16Fixed:
		case TD::OP_PixelFormat::RGBA16Float:
		case TD::OP_PixelFormat::RGBA32Float:
		case TD::OP_PixelFormat::Mono8Fixed:
		case TD::OP_PixelFormat::Mono16Fixed:
		case TD::OP_PixelFormat::Mono16Float:
		case TD::OP_PixelFormat::Mono32Float:
		case TD::OP_PixelFormat::RG8Fixed:
		case TD::OP_PixelFormat::RG16Fixed:
		case TD::OP_PixelFormat::RG16Float:
		case TD::OP_PixelFormat::RG32Float:
		case TD::OP_PixelFormat::A8Fixed:
		case TD::OP_PixelFormat::A16Fixed:
		case TD::OP_PixelFormat::A16Float:
		case TD::OP_PixelFormat::A32Float:
		case TD::OP_PixelFormat::MonoA8Fixed:
		case TD::OP_PixelFormat::MonoA16Fixed:
		case TD::OP_PixelFormat::MonoA16Float:
		case TD::OP_PixelFormat::MonoA32Float:
		case TD::OP_PixelFormat::RGB10A2Fixed:
		case TD::OP_PixelFormat::RGB11Float:
			return true;
		default:
			return false;
	}
}

inline TD::OP_PixelFormat
supportedOrBGRA8(TD::OP_PixelFormat format)
{
	return isSupportedOutputFormat(format)
		? format
		: TD::OP_PixelFormat::BGRA8Fixed;
}

inline bool
commonFormatUsesInput(const TD::OP_Inputs* inputs)
{
	const char* format = inputs ? inputs->getParString("format") : nullptr;
	return format && std::strcmp(format, "useinput") == 0;
}

inline uint32_t
bytesPerPixel(TD::OP_PixelFormat format)
{
	switch (format)
	{
		case TD::OP_PixelFormat::BGRA8Fixed:
		case TD::OP_PixelFormat::RGBA8Fixed:
		case TD::OP_PixelFormat::Mono32Float:
		case TD::OP_PixelFormat::RG16Fixed:
		case TD::OP_PixelFormat::RG16Float:
		case TD::OP_PixelFormat::A32Float:
		case TD::OP_PixelFormat::MonoA16Fixed:
		case TD::OP_PixelFormat::MonoA16Float:
		case TD::OP_PixelFormat::RGB10A2Fixed:
		case TD::OP_PixelFormat::RGB11Float:
			return 4;
		case TD::OP_PixelFormat::RGBA16Fixed:
		case TD::OP_PixelFormat::RGBA16Float:
		case TD::OP_PixelFormat::RG32Float:
		case TD::OP_PixelFormat::MonoA32Float:
			return 8;
		case TD::OP_PixelFormat::RGBA32Float:
			return 16;
		case TD::OP_PixelFormat::Mono8Fixed:
		case TD::OP_PixelFormat::A8Fixed:
			return 1;
		case TD::OP_PixelFormat::Mono16Fixed:
		case TD::OP_PixelFormat::Mono16Float:
		case TD::OP_PixelFormat::RG8Fixed:
		case TD::OP_PixelFormat::A16Fixed:
		case TD::OP_PixelFormat::A16Float:
		case TD::OP_PixelFormat::MonoA8Fixed:
			return 2;
		default:
			return 0;
	}
}

inline TD::OP_TextureDesc
resolvedDesc(
	TD::TOP_Output* output,
	uint32_t fallbackWidth,
	uint32_t fallbackHeight,
	TD::OP_PixelFormat fallbackFormat,
	bool honorCommonSize,
	const TD::OP_Inputs* inputs = nullptr,
	TD::OP_PixelFormat useInputFormat = TD::OP_PixelFormat::Invalid)
{
	TD::OP_TextureDesc suggested;
	output->getSuggestedOutputDesc(&suggested, nullptr);
	const bool useExplicitInputFormat =
		commonFormatUsesInput(inputs) &&
		useInputFormat != TD::OP_PixelFormat::Invalid;
	const bool useFallbackFormat =
		suggested.pixelFormat == TD::OP_PixelFormat::Invalid;

	TD::OP_TextureDesc desc;
	desc.width = honorCommonSize && suggested.width > 0
		? suggested.width
		: fallbackWidth;
	desc.height = honorCommonSize && suggested.height > 0
		? suggested.height
		: fallbackHeight;
	desc.depth = 1;
	desc.texDim = TD::OP_TexDim::e2D;
	if (useExplicitInputFormat)
		desc.pixelFormat = supportedOrBGRA8(useInputFormat);
	else if (useFallbackFormat)
		desc.pixelFormat = supportedOrBGRA8(fallbackFormat);
	else
		desc.pixelFormat = supportedOrBGRA8(suggested.pixelFormat);
	if (suggested.aspectX > 0.0f && suggested.aspectY > 0.0f)
	{
		desc.aspectX = suggested.aspectX;
		desc.aspectY = suggested.aspectY;
	}
	else if (!honorCommonSize && suggested.width > 0 && suggested.height > 0)
	{
		desc.aspectX = static_cast<float>(suggested.width);
		desc.aspectY = static_cast<float>(suggested.height);
	}
	else
	{
		desc.aspectX = static_cast<float>(desc.width);
		desc.aspectY = static_cast<float>(desc.height);
	}

	if (desc.width == 0)
		desc.width = fallbackWidth;
	if (desc.height == 0)
		desc.height = fallbackHeight;

	return desc;
}

inline uint8_t
to8(float value)
{
	value = std::clamp(value, 0.0f, 1.0f);
	return static_cast<uint8_t>(std::lround(value * 255.0f));
}

inline uint16_t
to16(float value)
{
	value = std::clamp(value, 0.0f, 1.0f);
	return static_cast<uint16_t>(std::lround(value * 65535.0f));
}

inline uint16_t
to10(float value)
{
	value = std::clamp(value, 0.0f, 1.0f);
	return static_cast<uint16_t>(std::lround(value * 1023.0f));
}

inline uint8_t
to2(float value)
{
	value = std::clamp(value, 0.0f, 1.0f);
	return static_cast<uint8_t>(std::lround(value * 3.0f));
}

inline uint16_t
floatToHalf(float value)
{
	uint32_t bits = 0;
	std::memcpy(&bits, &value, sizeof(bits));

	uint32_t sign = (bits >> 16) & 0x8000u;
	int32_t exponent = static_cast<int32_t>((bits >> 23) & 0xffu) - 127 + 15;
	uint32_t mantissa = bits & 0x7fffffu;

	if (exponent <= 0)
	{
		if (exponent < -10)
			return static_cast<uint16_t>(sign);

		mantissa |= 0x800000u;
		uint32_t shifted = mantissa >> (1 - exponent + 13);
		return static_cast<uint16_t>(sign | shifted);
	}

	if (exponent >= 31)
		return static_cast<uint16_t>(sign | 0x7c00u);

	return static_cast<uint16_t>(
		sign |
		(static_cast<uint32_t>(exponent) << 10) |
		(mantissa >> 13));
}

inline uint32_t
floatToUnsignedPacked(float value, uint32_t mantissaBits)
{
	value = std::max(value, 0.0f);
	if (value == 0.0f)
		return 0;

	uint32_t bits = 0;
	std::memcpy(&bits, &value, sizeof(bits));

	int32_t exponent = static_cast<int32_t>((bits >> 23) & 0xffu) - 127 + 15;
	uint32_t mantissa = bits & 0x7fffffu;
	if (exponent <= 0)
	{
		if (exponent < -static_cast<int32_t>(mantissaBits))
			return 0;

		mantissa |= 0x800000u;
		const uint32_t shift =
			static_cast<uint32_t>(1 - exponent) + (23u - mantissaBits);
		return mantissa >> shift;
	}

	const uint32_t maxExponent = 31u;
	if (exponent >= static_cast<int32_t>(maxExponent))
		return maxExponent << mantissaBits;

	return (static_cast<uint32_t>(exponent) << mantissaBits) |
		(mantissa >> (23u - mantissaBits));
}

template <typename T>
inline void
writeValue(uint8_t*& dst, T value)
{
	std::memcpy(dst, &value, sizeof(T));
	dst += sizeof(T);
}

inline void
writeFloat16(uint8_t*& dst, float value)
{
	writeValue<uint16_t>(dst, floatToHalf(value));
}

inline void
writeFloat32(uint8_t*& dst, float value)
{
	writeValue<float>(dst, value);
}

inline void
packPixel(
	uint8_t*& dst,
	TD::OP_PixelFormat format,
	float r,
	float g,
	float b,
	float a)
{
	const float mono = 0.2126f * r + 0.7152f * g + 0.0722f * b;
	const uint8_t b8 = to8(b);
	const uint8_t g8 = to8(g);
	const uint8_t r8 = to8(r);
	const uint8_t a8 = to8(a);

	switch (format)
	{
		case TD::OP_PixelFormat::BGRA8Fixed:
			*dst++ = b8;
			*dst++ = g8;
			*dst++ = r8;
			*dst++ = a8;
			break;
		case TD::OP_PixelFormat::RGBA8Fixed:
			*dst++ = r8;
			*dst++ = g8;
			*dst++ = b8;
			*dst++ = a8;
			break;
		case TD::OP_PixelFormat::RGBA16Fixed:
			writeValue<uint16_t>(dst, to16(r));
			writeValue<uint16_t>(dst, to16(g));
			writeValue<uint16_t>(dst, to16(b));
			writeValue<uint16_t>(dst, to16(a));
			break;
		case TD::OP_PixelFormat::RGBA16Float:
			writeFloat16(dst, r);
			writeFloat16(dst, g);
			writeFloat16(dst, b);
			writeFloat16(dst, a);
			break;
		case TD::OP_PixelFormat::RGBA32Float:
			writeFloat32(dst, r);
			writeFloat32(dst, g);
			writeFloat32(dst, b);
			writeFloat32(dst, a);
			break;
		case TD::OP_PixelFormat::Mono8Fixed:
			*dst++ = to8(mono);
			break;
		case TD::OP_PixelFormat::Mono16Fixed:
			writeValue<uint16_t>(dst, to16(mono));
			break;
		case TD::OP_PixelFormat::Mono16Float:
			writeFloat16(dst, mono);
			break;
		case TD::OP_PixelFormat::Mono32Float:
			writeFloat32(dst, mono);
			break;
		case TD::OP_PixelFormat::RG8Fixed:
			*dst++ = r8;
			*dst++ = g8;
			break;
		case TD::OP_PixelFormat::RG16Fixed:
			writeValue<uint16_t>(dst, to16(r));
			writeValue<uint16_t>(dst, to16(g));
			break;
		case TD::OP_PixelFormat::RG16Float:
			writeFloat16(dst, r);
			writeFloat16(dst, g);
			break;
		case TD::OP_PixelFormat::RG32Float:
			writeFloat32(dst, r);
			writeFloat32(dst, g);
			break;
		case TD::OP_PixelFormat::A8Fixed:
			*dst++ = a8;
			break;
		case TD::OP_PixelFormat::A16Fixed:
			writeValue<uint16_t>(dst, to16(a));
			break;
		case TD::OP_PixelFormat::A16Float:
			writeFloat16(dst, a);
			break;
		case TD::OP_PixelFormat::A32Float:
			writeFloat32(dst, a);
			break;
		case TD::OP_PixelFormat::MonoA8Fixed:
			*dst++ = to8(mono);
			*dst++ = a8;
			break;
		case TD::OP_PixelFormat::MonoA16Fixed:
			writeValue<uint16_t>(dst, to16(mono));
			writeValue<uint16_t>(dst, to16(a));
			break;
		case TD::OP_PixelFormat::MonoA16Float:
			writeFloat16(dst, mono);
			writeFloat16(dst, a);
			break;
		case TD::OP_PixelFormat::MonoA32Float:
			writeFloat32(dst, mono);
			writeFloat32(dst, a);
			break;
		case TD::OP_PixelFormat::RGB10A2Fixed:
		{
			const uint32_t packed =
				static_cast<uint32_t>(to10(r)) |
				(static_cast<uint32_t>(to10(g)) << 10) |
				(static_cast<uint32_t>(to10(b)) << 20) |
				(static_cast<uint32_t>(to2(a)) << 30);
			writeValue<uint32_t>(dst, packed);
			break;
		}
		case TD::OP_PixelFormat::RGB11Float:
		{
			const uint32_t packed =
				floatToUnsignedPacked(r, 6) |
				(floatToUnsignedPacked(g, 6) << 11) |
				(floatToUnsignedPacked(b, 5) << 22);
			writeValue<uint32_t>(dst, packed);
			break;
		}
		default:
			break;
	}
}

inline void
packBGRAPixel(
	uint8_t*& dst,
	TD::OP_PixelFormat format,
	uint8_t b8,
	uint8_t g8,
	uint8_t r8,
	uint8_t a8)
{
	packPixel(
		dst,
		format,
		static_cast<float>(r8) / 255.0f,
		static_cast<float>(g8) / 255.0f,
		static_cast<float>(b8) / 255.0f,
		static_cast<float>(a8) / 255.0f);
}

inline TD::OP_SmartRef<TD::TOP_Buffer>
createPackedBGRA8Buffer(
	TD::TOP_Context* context,
	const uint8_t* source,
	uint32_t sourceBytesPerRow,
	const TD::OP_TextureDesc& desc)
{
	const uint32_t bpp = bytesPerPixel(desc.pixelFormat);
	const uint64_t size =
		static_cast<uint64_t>(desc.width) *
		static_cast<uint64_t>(desc.height) *
		static_cast<uint64_t>(bpp);

	TD::OP_SmartRef<TD::TOP_Buffer> buffer =
		context->createOutputBuffer(size, TD::TOP_BufferFlags::None, nullptr);

	uint8_t* dst = static_cast<uint8_t*>(buffer->data);
	for (uint32_t y = 0; y < desc.height; ++y)
	{
		const uint8_t* srcRow = source + static_cast<uint64_t>(y) * sourceBytesPerRow;
		for (uint32_t x = 0; x < desc.width; ++x)
		{
			const uint8_t* src = srcRow + static_cast<uint64_t>(x) * 4;
			packBGRAPixel(dst, desc.pixelFormat, src[0], src[1], src[2], src[3]);
		}
	}

	return buffer;
}

inline TD::OP_SmartRef<TD::TOP_Buffer>
createPackedMono8Buffer(
	TD::TOP_Context* context,
	const uint8_t* source,
	uint32_t sourceBytesPerRow,
	const TD::OP_TextureDesc& desc)
{
	const uint32_t bpp = bytesPerPixel(desc.pixelFormat);
	const uint64_t size =
		static_cast<uint64_t>(desc.width) *
		static_cast<uint64_t>(desc.height) *
		static_cast<uint64_t>(bpp);

	TD::OP_SmartRef<TD::TOP_Buffer> buffer =
		context->createOutputBuffer(size, TD::TOP_BufferFlags::None, nullptr);

	uint8_t* dst = static_cast<uint8_t*>(buffer->data);
	for (uint32_t y = 0; y < desc.height; ++y)
	{
		const uint8_t* srcRow = source + static_cast<uint64_t>(y) * sourceBytesPerRow;
		for (uint32_t x = 0; x < desc.width; ++x)
		{
			const float value = static_cast<float>(srcRow[x]) / 255.0f;
			packPixel(dst, desc.pixelFormat, value, value, value, 1.0f);
		}
	}

	return buffer;
}

inline TD::OP_SmartRef<TD::TOP_Buffer>
createPackedMono32Buffer(
	TD::TOP_Context* context,
	const float* source,
	uint32_t sourceBytesPerRow,
	const TD::OP_TextureDesc& desc)
{
	const uint32_t bpp = bytesPerPixel(desc.pixelFormat);
	const uint64_t size =
		static_cast<uint64_t>(desc.width) *
		static_cast<uint64_t>(desc.height) *
		static_cast<uint64_t>(bpp);

	TD::OP_SmartRef<TD::TOP_Buffer> buffer =
		context->createOutputBuffer(size, TD::TOP_BufferFlags::None, nullptr);

	uint8_t* dst = static_cast<uint8_t*>(buffer->data);
	for (uint32_t y = 0; y < desc.height; ++y)
	{
		const float* srcRow = reinterpret_cast<const float*>(
			reinterpret_cast<const uint8_t*>(source) +
			static_cast<uint64_t>(y) * sourceBytesPerRow);
		for (uint32_t x = 0; x < desc.width; ++x)
		{
			const float value = srcRow[x];
			packPixel(dst, desc.pixelFormat, value, value, value, 1.0f);
		}
	}

	return buffer;
}

inline TD::OP_SmartRef<TD::TOP_Buffer>
createPackedRGBA32Buffer(
	TD::TOP_Context* context,
	const float* source,
	uint32_t sourceBytesPerRow,
	const TD::OP_TextureDesc& desc)
{
	const uint32_t bpp = bytesPerPixel(desc.pixelFormat);
	const uint64_t size =
		static_cast<uint64_t>(desc.width) *
		static_cast<uint64_t>(desc.height) *
		static_cast<uint64_t>(bpp);

	TD::OP_SmartRef<TD::TOP_Buffer> buffer =
		context->createOutputBuffer(size, TD::TOP_BufferFlags::None, nullptr);

	uint8_t* dst = static_cast<uint8_t*>(buffer->data);
	for (uint32_t y = 0; y < desc.height; ++y)
	{
		const float* srcRow = reinterpret_cast<const float*>(
			reinterpret_cast<const uint8_t*>(source) +
			static_cast<uint64_t>(y) * sourceBytesPerRow);
		for (uint32_t x = 0; x < desc.width; ++x)
		{
			const float* src = srcRow + static_cast<uint64_t>(x) * 4;
			packPixel(dst, desc.pixelFormat, src[0], src[1], src[2], src[3]);
		}
	}

	return buffer;
}

inline void
uploadBGRA8(
	TD::TOP_Context* context,
	TD::TOP_Output* output,
	const uint8_t* source,
	uint32_t sourceBytesPerRow,
	const TD::OP_TextureDesc& desc,
	TD::TOP_FirstPixel firstPixel)
{
	TD::OP_SmartRef<TD::TOP_Buffer> buffer =
		createPackedBGRA8Buffer(context, source, sourceBytesPerRow, desc);

	TD::TOP_UploadInfo info;
	info.textureDesc = desc;
	info.firstPixel = firstPixel;
	info.colorBufferIndex = 0;

	output->uploadBuffer(&buffer, info, nullptr);
}

inline void
uploadMono8(
	TD::TOP_Context* context,
	TD::TOP_Output* output,
	const uint8_t* source,
	uint32_t sourceBytesPerRow,
	const TD::OP_TextureDesc& desc,
	TD::TOP_FirstPixel firstPixel)
{
	TD::OP_SmartRef<TD::TOP_Buffer> buffer =
		createPackedMono8Buffer(context, source, sourceBytesPerRow, desc);

	TD::TOP_UploadInfo info;
	info.textureDesc = desc;
	info.firstPixel = firstPixel;
	info.colorBufferIndex = 0;

	output->uploadBuffer(&buffer, info, nullptr);
}

inline void
uploadMono32(
	TD::TOP_Context* context,
	TD::TOP_Output* output,
	const float* source,
	uint32_t sourceBytesPerRow,
	const TD::OP_TextureDesc& desc,
	TD::TOP_FirstPixel firstPixel)
{
	TD::OP_SmartRef<TD::TOP_Buffer> buffer =
		createPackedMono32Buffer(context, source, sourceBytesPerRow, desc);

	TD::TOP_UploadInfo info;
	info.textureDesc = desc;
	info.firstPixel = firstPixel;
	info.colorBufferIndex = 0;

	output->uploadBuffer(&buffer, info, nullptr);
}

inline void
uploadRGBA32(
	TD::TOP_Context* context,
	TD::TOP_Output* output,
	const float* source,
	uint32_t sourceBytesPerRow,
	const TD::OP_TextureDesc& desc,
	TD::TOP_FirstPixel firstPixel)
{
	TD::OP_SmartRef<TD::TOP_Buffer> buffer =
		createPackedRGBA32Buffer(context, source, sourceBytesPerRow, desc);

	TD::TOP_UploadInfo info;
	info.textureDesc = desc;
	info.firstPixel = firstPixel;
	info.colorBufferIndex = 0;

	output->uploadBuffer(&buffer, info, nullptr);
}
}
}
