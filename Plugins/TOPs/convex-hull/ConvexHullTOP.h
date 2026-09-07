#pragma once

#include "TOP_CPlusPlusBase.h"
#include "HullEngine.h"
#include <array>
#include <string>

class ConvexHullTOP final : public TD::TOP_CPlusPlusBase
{
public:
    explicit ConvexHullTOP(TD::TOP_Context* context) : context_(context) {}
    void getGeneralInfo(TD::TOP_GeneralInfo*, const TD::OP_Inputs*, void*) override;
    void execute(TD::TOP_Output*, const TD::OP_Inputs*, void*) override;
    void setupParameters(TD::OP_ParameterManager*, void*) override;
    void getErrorString(TD::OP_String*, void*) override;
    void getWarningString(TD::OP_String*, void*) override;
    int32_t getNumInfoCHOPChans(void*) override { return 36; }
    void getInfoCHOPChan(int32_t, TD::OP_InfoCHOPChan*, void*) override;

private:
    TD::TOP_Context* context_;
    Hull::Engine engine_;
    std::string error_, warning_;
    std::array<float, 36> stats_{};
};
