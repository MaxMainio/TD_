#pragma once

#include <string>

namespace TD
{
	class OP_Inputs;
	class OP_ParameterManager;
}

constexpr static char ModeName[] = "Mode";
constexpr static char ModeLabel[] = "Mode";

constexpr static char ChannelName[] = "Channel";
constexpr static char ChannelLabel[] = "Channel";

enum class ModeMenuItems
{
	Minimum,
	Maximum
};

enum class ChannelMenuItems
{
	Red,
	Green,
	Blue,
	Alpha
};

class Parameters
{
public:
	static void setup(TD::OP_ParameterManager*);

	static ModeMenuItems evalMode(const TD::OP_Inputs* inputs);
	static ChannelMenuItems evalChannel(const TD::OP_Inputs* inputs);
};