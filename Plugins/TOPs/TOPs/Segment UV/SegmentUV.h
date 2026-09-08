#pragma once
#include "TOP_CPlusPlusBase.h"
#include "SegmentInfoDAT.h"
#include <string>

class SegmentUV final : public TD::TOP_CPlusPlusBase
{
public:
    SegmentUV(const TD::OP_NodeInfo*, TD::TOP_Context*);
    void getGeneralInfo(TD::TOP_GeneralInfo*, const TD::OP_Inputs*, void*) override;
    void execute(TD::TOP_Output*, const TD::OP_Inputs*, void*) override;
    void setupParameters(TD::OP_ParameterManager*, void*) override;
    void getErrorString(TD::OP_String*, void*) override;
    void getWarningString(TD::OP_String*, void*) override;
    bool getInfoDATSize(TD::OP_InfoDATSize*, void*) override;
    void getInfoDATEntries(int32_t, int32_t, TD::OP_InfoDATEntries*, void*) override;
    void pulsePressed(const char*, void*) override;
    void setDockWarning(const char* message) { dockWarning_ = message; }
private:
    void requestDocking(bool resetLayout = false);
    TD::TOP_Context* context_;
    uint32_t nodeId_;
    Segment::InfoTable table_;
    std::string error_, dockWarning_;
};
