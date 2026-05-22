#include <string>
#include <array>
#include "CPlusPlus_Common.h"
#include "Parameters.h"

int
Parameters::evalCarvewidth(const TD::OP_Inputs* inputs)
{
	return inputs->getParInt(CarvewidthName);
}

CostUpdateMenuItems
Parameters::evalCostupdate(const TD::OP_Inputs* inputs)
{
	return static_cast<CostUpdateMenuItems>(inputs->getParInt(CostupdateName));
}

void
Parameters::setup(TD::OP_ParameterManager* manager)
{
	{
		TD::OP_NumericParameter p;
		p.name = CarvewidthName;
		p.label = CarvewidthLabel;
		p.page = "Carving";
		p.defaultValues[0] = 0;
		p.minSliders[0] = -256.0;
		p.maxSliders[0] = 256.0;
		p.minValues[0] = -256.0;
		p.maxValues[0] = 256.0;
		p.clampMins[0] = true;
		p.clampMaxes[0] = true;

		TD::OP_ParAppendResult res = manager->appendInt(p);
		assert(res == TD::OP_ParAppendResult::Success);
	}

	{
		TD::OP_StringParameter p;
		p.name = CostupdateName;
		p.label = CostupdateLabel;
		p.page = "Carving";
		p.defaultValue = "Everyseam";

		std::array<const char*, 2> names =
		{
			"Once",
			"Everyseam"
		};

		std::array<const char*, 2> labels =
		{
			"Once",
			"Every Seam"
		};

		TD::OP_ParAppendResult res = manager->appendMenu(p, names.size(), names.data(), labels.data());
		assert(res == TD::OP_ParAppendResult::Success);
	}
}