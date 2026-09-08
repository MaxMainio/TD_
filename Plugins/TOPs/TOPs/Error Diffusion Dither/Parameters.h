#pragma once

#include "TOP_CPlusPlusBase.h"
#include "DitherEngine.h"

namespace Dither
{
void setupParameters(TD::OP_ParameterManager* manager);
Settings evaluateParameters(const TD::OP_Inputs* inputs);
}
