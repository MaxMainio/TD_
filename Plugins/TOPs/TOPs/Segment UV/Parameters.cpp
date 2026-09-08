#include <string>
#include <array>
#include <algorithm>
#include "CPlusPlus_Common.h"
#include "Parameters.h"

MethodMenuItems
Parameters::evalMethod(const TD::OP_Inputs* inputs)
{
	return static_cast<MethodMenuItems>(inputs->getParInt(MethodName));
}

int
Parameters::evalSeed(const TD::OP_Inputs* inputs)
{
	return inputs->getParInt(SeedName);
}

double
Parameters::evalAlphathreshold(const TD::OP_Inputs* inputs)
{
	return inputs->getParDouble(AlphathresholdName);
}

void
Parameters::setup(TD::OP_ParameterManager* manager)
{
	{
		TD::OP_StringParameter p;
		p.name = MethodName;
		p.label = MethodLabel;
		p.page = "Segmentation";
		p.defaultValue = "Closestsegmentpixeltocentroid";

		std::array<const char*, 5> names =
		{
			"Centroid",
			"Boundingboxcenter",
			"Medianpixelcoordinate",
			"Closestsegmentpixeltocentroid",
			"Random"
		};

		std::array<const char*, 5> labels =
		{
			"Centroid",
			"Bounding-Box Center",
			"Median Pixel Coordinate",
			"Closest Segment Pixel to Centroid",
			"Random"
		};

		TD::OP_ParAppendResult res = manager->appendMenu(p, names.size(), names.data(), labels.data());
		assert(res == TD::OP_ParAppendResult::Success);
	}

	{
		TD::OP_NumericParameter p;
		p.name = SeedName;
		p.label = SeedLabel;
		p.page = "Segmentation";
		p.defaultValues[0] = 1;
		p.minSliders[0] = 1.0;
		p.maxSliders[0] = 9999.0;
		p.minValues[0] = 1.0;
		p.maxValues[0] = 9999.0;
		p.clampMins[0] = true;
		p.clampMaxes[0] = true;

		TD::OP_ParAppendResult res = manager->appendInt(p);
		assert(res == TD::OP_ParAppendResult::Success);
}

	{
		TD::OP_NumericParameter p;
		p.name = AlphathresholdName;
		p.label = AlphathresholdLabel;
		p.page = "Segmentation";
		p.defaultValues[0] = 0.5;
		p.minSliders[0] = 0.0;
		p.maxSliders[0] = 1.0;
		p.minValues[0] = 0.0;
		p.maxValues[0] = 1.0;
		p.clampMins[0] = true;
		p.clampMaxes[0] = true;

		TD::OP_ParAppendResult res = manager->appendFloat(p);
		assert(res == TD::OP_ParAppendResult::Success);
	}

    {
        TD::OP_StringParameter p;
        p.name = "Datdetail";
        p.label = "DAT Detail";
        p.page = "Output";
        p.defaultValue = "Basic";
        const char* choices[] = {"Basic", "Medium", "Maximum"};
        const auto res = manager->appendMenu(p, 3, choices, choices);
        assert(res == TD::OP_ParAppendResult::Success);
        (void)res;
    }
    {
        TD::OP_NumericParameter p;
        p.name = "Repairsegmentdat";
        p.label = "Create/Repair Segment DAT";
        p.page = "Output";
        const auto res = manager->appendPulse(p);
        assert(res == TD::OP_ParAppendResult::Success);
        (void)res;
    }
}

Segment::DATDetail
Parameters::evalDATDetail(const TD::OP_Inputs* inputs)
{
    return inputs ? Segment::DATDetail(std::clamp(inputs->getParInt("Datdetail"), 0, 2))
                  : Segment::DATDetail::Basic;
}
