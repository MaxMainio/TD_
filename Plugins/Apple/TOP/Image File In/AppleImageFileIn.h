#pragma once
#include "TOP_CPlusPlusBase.h"
#include "ImageData.h"
#include "Host.h"
class AppleImageFileIn final:public TD::TOP_CPlusPlusBase {
public:
    AppleImageFileIn(const TD::OP_NodeInfo*,TD::TOP_Context*);
    void getGeneralInfo(TD::TOP_GeneralInfo*,const TD::OP_Inputs*,void*) override;
    void setupParameters(TD::OP_ParameterManager*,void*) override;
    void setParameterEnableStates(const TD::OP_Inputs*,TD::OP_ParEnableState*,void*) override;
    void buildDynamicMenu(const TD::OP_Inputs*,TD::OP_BuildDynamicMenuInfo*,void*) override;
    void execute(TD::TOP_Output*,const TD::OP_Inputs*,void*) override;
    void pulsePressed(const char*,void*) override;
    void getErrorString(TD::OP_String*,void*) override;
    void getWarningString(TD::OP_String*,void*) override;
    void getInfoPopupString(TD::OP_String*,void*) override;
    bool getInfoDATSize(TD::OP_InfoDATSize*,void*) override;
    void getInfoDATEntries(int32_t,int32_t,TD::OP_InfoDATEntries*,void*) override;
    int32_t getNumInfoCHOPChans(void*) override {return 29;}
    void getInfoCHOPChan(int32_t,TD::OP_InfoCHOPChan*,void*) override;
    void setDockWarning(const char* s){dockWarning_=s;}
    std::shared_ptr<const ImageFile::Result> snapshot() const{return published_;}
    uint32_t availableMask() const;
    bool setDemand(uint32_t mask){mask&=ImageFile::AllAuxiliaries;if(mask==demandMask_)return false;demandMask_=mask;return true;}
    bool needsPublish() const {return engine_.result()->revision!=revision_ || uploadedWorkingSpace_!=node_->workingColorSpace || demandMask_!=publishedDemand_;}
private:
    void upload(TD::TOP_Output*,const TD::OP_Inputs*,const ImageFile::Result&);
    TD::TOP_Context* context_;
    const TD::OP_NodeInfo* node_;
    TD::OP_WorkingColorSpace uploadedWorkingSpace_=TD::OP_WorkingColorSpace::Passthrough;
    ImageFile::Host host_;
    ImageFile::Engine engine_;
    std::shared_ptr<const ImageFile::Result> published_=std::make_shared<ImageFile::Result>();
    std::vector<ImageFile::Row> rows_;
    std::string error_,warning_,dockWarning_;
    TD::OP_TextureDesc colorDesc_;
    std::array<std::string,ImageFile::BufferCount> uploadSignatures_;
    uint32_t demandMask_=0,publishedDemand_=UINT32_MAX,activeMask_=0;
    uint64_t auxiliaryUploads_=0;
    bool refresh_=false,initial_=true;
    uint64_t revision_=UINT64_MAX;
};
