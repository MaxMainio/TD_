#include "../TOP_CPlusPlusBase.h"
#include <vector>
#include <mutex>
#include <atomic>
#include <string>
#import <Vision/Vision.h>
#import <CoreML/CoreML.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>

// Access the scalar conversion without loading a Core ML model or TD host.
#define private public
#include "../AppleDepth.h"
#undef private

#include <cmath>
#include <cstring>
#include <iostream>

int main()
{
	for (uint32_t bits = 0; bits <= 0xffff; ++bits)
	{
		const uint16_t encoded = static_cast<uint16_t>(bits);
		_Float16 native;
		static_assert(sizeof(native) == sizeof(encoded));
		std::memcpy(&native, &encoded, sizeof(encoded));
		const float expected = static_cast<float>(native);
		const float actual = AppleDepth::halfToFloat(encoded);
		const bool matches = std::isnan(expected)
			? std::isnan(actual)
			: actual == expected && std::signbit(actual) == std::signbit(expected);
		if (!matches)
		{
			std::cerr << "Half-float mismatch at " << bits << ": "
				<< actual << " != " << expected << '\n';
			return 1;
		}
	}
	std::cout << "All 65,536 half-float encodings passed\n";
	return 0;
}
