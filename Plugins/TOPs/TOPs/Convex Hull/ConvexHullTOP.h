#pragma once

#include "TOP_CPlusPlusBase.h"
#include "HullEngine.h"
#include "HullInfoDAT.h"
#include <array>
#include <string>

class ConvexHullTOP final : public TD::TOP_CPlusPlusBase
{
public:
    ConvexHullTOP(const TD::OP_NodeInfo*, TD::TOP_Context*);
    void getGeneralInfo(TD::TOP_GeneralInfo*, const TD::OP_Inputs*, void*) override;
    void execute(TD::TOP_Output*, const TD::OP_Inputs*, void*) override;
    void setupParameters(TD::OP_ParameterManager*, void*) override;
    void getErrorString(TD::OP_String*, void*) override;
    void getWarningString(TD::OP_String*, void*) override;
    int32_t getNumInfoCHOPChans(void*) override { return 36; }
    void getInfoCHOPChan(int32_t, TD::OP_InfoCHOPChan*, void*) override;
    bool getInfoDATSize(TD::OP_InfoDATSize*, void*) override;
    void getInfoDATEntries(int32_t, int32_t, TD::OP_InfoDATEntries*, void*) override;
    void pulsePressed(const char*, void*) override;
    void setDockWarning(const char* message) { dockWarning_ = message; }

private:
    TD::TOP_Context* context_;
    Hull::Engine engine_;
    Hull::InfoTable table_;
    uint32_t nodeId_;
    void requestDocking(bool resetLayout = false);
    std::string dockWarning_;
    std::string error_, warning_;
    std::array<float, 36> stats_{};
};
