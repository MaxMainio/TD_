#pragma once
#include "TOP_CPlusPlusBase.h"
#include "HullEngine.h"
#include "HullInfoDAT.h"

namespace Hull
{
void setupParameters(TD::OP_ParameterManager* manager);
Settings evaluateParameters(const TD::OP_Inputs* inputs);
DATDetail evaluateDATDetail(const TD::OP_Inputs* inputs);
}
