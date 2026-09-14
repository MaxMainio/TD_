#pragma once
#include "CPlusPlus_Common.h"
#include <string>

namespace Hull
{
void configureDocking(TD::OP_CustomOPInfo& info);
std::string scheduleDocking(uint32_t nodeId, bool resetLayout);
}
