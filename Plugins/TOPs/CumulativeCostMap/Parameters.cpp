#include <string>
#include <array>
#include "CPlusPlus_Common.h"
#include "Parameters.h"

ModeMenuItems
Parameters::evalMode(const TD::OP_Inputs* inputs)
{
	return static_cast<ModeMenuItems>(inputs->getParInt(ModeName));
}

ChannelMenuItems
Parameters::evalChannel(const TD::OP_Inputs* inputs)
{
	return static_cast<ChannelMenuItems>(inputs->getParInt(ChannelName));
}

void
Parameters::setup(TD::OP_ParameterManager* manager)
{
	{
		TD::OP_StringParameter p;
		p.name = ModeName;
		p.label = ModeLabel;
		p.page = "Cost";
		p.defaultValue = "Minimum";

		std::array<const char*, 2> names =
		{
			"Minimum",
			"Maximum"
		};

		std::array<const char*, 2> labels =
		{
			"Minimum",
			"Maximum"
		};

		TD::OP_ParAppendResult res = manager->appendMenu(p, names.size(), names.data(), labels.data());
		assert(res == TD::OP_ParAppendResult::Success);
	}

	{
		TD::OP_StringParameter p;
		p.name = ChannelName;
		p.label = ChannelLabel;
		p.page = "Cost";
		p.defaultValue = "Red";

		std::array<const char*, 4> names =
		{
			"Red",
			"Green",
			"Blue",
			"Alpha"
		};

		std::array<const char*, 4> labels =
		{
			"Red",
			"Green",
			"Blue",
			"Alpha"
		};

		TD::OP_ParAppendResult res = manager->appendMenu(p, names.size(), names.data(), labels.data());
		assert(res == TD::OP_ParAppendResult::Success);
	}
}