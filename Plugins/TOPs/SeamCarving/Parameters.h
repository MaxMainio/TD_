#pragma once

#include <string>

namespace TD
{
	class OP_Inputs;
	class OP_ParameterManager;
}

constexpr static char CarvewidthName[] = "Carvewidth";
constexpr static char CarvewidthLabel[] = "Carve Width";

constexpr static char CostupdateName[] = "Costupdate";
constexpr static char CostupdateLabel[] = "Cost Update";

enum class CostUpdateMenuItems
{
	Once,
	Everyseam
};

class Parameters
{
public:
	static void setup(TD::OP_ParameterManager*);

	static int evalCarvewidth(const TD::OP_Inputs* inputs);
	static CostUpdateMenuItems evalCostupdate(const TD::OP_Inputs* inputs);
};