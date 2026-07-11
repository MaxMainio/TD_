#pragma once

#include <string>

namespace TD
{
	class OP_Inputs;
	class OP_ParameterManager;
}

constexpr static char MethodName[] = "Method";
constexpr static char MethodLabel[] = "Method";

constexpr static char SeedName[] = "Seed";
constexpr static char SeedLabel[] = "Seed";

constexpr static char AlphathresholdName[] = "Alphathreshold";
constexpr static char AlphathresholdLabel[] = "Alpha Threshold";

enum class MethodMenuItems
{
	Centroid,
	Boundingboxcenter,
	Medianpixelcoordinate,
	Closestsegmentpixeltocentroid,
	Random
};

class Parameters
{
public:
	static void setup(TD::OP_ParameterManager*);

	static MethodMenuItems evalMethod(const TD::OP_Inputs* inputs);
	static double evalAlphathreshold(const TD::OP_Inputs* inputs);
	static int evalSeed(const TD::OP_Inputs* inputs);
};