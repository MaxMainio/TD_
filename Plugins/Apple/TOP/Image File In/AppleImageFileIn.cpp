#include "AppleImageFileIn.h"
#include "TOPOutputHelper.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <stdexcept>

using namespace ImageFile;
namespace {
const char* parameter(const TD::OP_Inputs* i,const char* name,const char* fallback) {auto* s=i->getParString(name);return s&&*s?s:fallback;}
void check(TD::OP_ParAppendResult r){if(r!=TD::OP_ParAppendResult::Success)throw std::runtime_error("Could not create Image File In parameters");}
TD::OP_TextureDesc colorDesc(TD::TOP_Output* output,const TD::OP_Inputs* inputs,const Plane& p) {
    uint32_t w=p.present()?p.width:1,h=p.present()?p.height:1;
    auto desc=TDPlugin::TOPOutput::resolvedDesc(output,w,h,TD::OP_PixelFormat::RGBA16Float,true,inputs,TD::OP_PixelFormat::RGBA16Float);
    std::string resolution=parameter(inputs,"outputresolution","useinput");
    unsigned divisor=resolution=="half"?2:resolution=="quarter"?4:resolution=="eighth"?8:1;
    unsigned multiplier=resolution=="2x"?2:resolution=="4x"?4:resolution=="8x"?8:1;
    if(resolution=="useinput"||divisor>1||multiplier>1||resolution=="fit"||resolution=="limit") {
        double scale=double(multiplier)/divisor;
        if(resolution=="fit"||resolution=="limit") {
            scale=std::min(double(std::max(1,inputs->getParInt("resolution",0)))/w,
                           double(std::max(1,inputs->getParInt("resolution",1)))/h);
            if(resolution=="limit")scale=std::min(1.,scale);
        }
        desc.width=std::max(1u,uint32_t(std::floor(w*scale)));desc.height=std::max(1u,uint32_t(std::floor(h*scale)));
        if(std::string(parameter(inputs,"outputaspect","resolution"))!="custom") {desc.aspectX=float(desc.width);desc.aspectY=float(desc.height);}
    }
    return desc;
}
float sample(const Plane& p,uint32_t x,uint32_t y,uint32_t w,uint32_t h,uint32_t c) {
    if(w==p.width&&h==p.height)return (*p.pixels)[(size_t(y)*w+x)*p.channels+c];
    double sx=std::clamp((x+.5)*p.width/w-.5,0.,double(p.width-1));
    double sy=std::clamp((y+.5)*p.height/h-.5,0.,double(p.height-1));
    uint32_t x0=uint32_t(sx),y0=uint32_t(sy),x1=std::min(x0+1,p.width-1),y1=std::min(y0+1,p.height-1);
    float fx=float(sx-x0),fy=float(sy-y0);
    auto at=[&](uint32_t xx,uint32_t yy){return (*p.pixels)[(size_t(yy)*p.width+xx)*p.channels+c];};
    return (1-fy)*((1-fx)*at(x0,y0)+fx*at(x1,y0))+fy*((1-fx)*at(x0,y1)+fx*at(x1,y1));
}
}
extern "C" {
DLLEXPORT void FillTOPPluginInfo(TD::TOP_PluginInfo* info) {
    if(!info->setAPIVersion(TD::TOPCPlusPlusAPIVersion))return;
    info->executeMode=TD::TOP_ExecuteMode::CPUMem;
    auto& c=info->customOPInfo;c.opType->setString("Appleimagefilein");c.opLabel->setString("Apple Image In");c.opIcon->setString("AIF");
    c.authorName->setString("Max Mainio Beidler");c.minInputs=c.maxInputs=0;c.minorVersion=2;configurePython(c);
}
DLLEXPORT TD::TOP_CPlusPlusBase* CreateTOPInstance(const TD::OP_NodeInfo* n,TD::TOP_Context* c){return new AppleImageFileIn(n,c);}
DLLEXPORT void DestroyTOPInstance(TD::TOP_CPlusPlusBase* p,TD::TOP_Context*){delete p;}
}
AppleImageFileIn::AppleImageFileIn(const TD::OP_NodeInfo* node,TD::TOP_Context* c):context_(c),node_(node),host_(node->opId) {}
void AppleImageFileIn::getGeneralInfo(TD::TOP_GeneralInfo* info,const TD::OP_Inputs*,void*) {
    info->cookEveryFrame=false;info->cookEveryFrameIfAsked=true;info->inputSizeIndex=-1;
}
void AppleImageFileIn::setupParameters(TD::OP_ParameterManager* m,void*) {
    TD::OP_StringParameter p;p.page="Source";p.name="File";p.label="File";p.defaultValue="";
    p.help="One local image file, including HEIC/HEIF and Image I/O-supported camera formats.";check(m->appendFile(p));
    p.name="Image";p.label="Image";p.defaultValue="primary";p.help="Select a contained image or page. The primary image is selected by default.";check(m->appendDynamicStringMenu(p));
    p.name="Decode";p.label="Decode";p.defaultValue="auto";p.help="Auto preserves HDR when available. SDR requests Apple's SDR rendering.";
    const char* names[]={"auto","hdr","sdr"};const char* labels[]={"Auto","HDR","SDR"};check(m->appendMenu(p,3,names,labels));
    p.page="Technical";p.name="Auxiliaryoutputs";p.label="Auxiliary Outputs";p.defaultValue="automatic";
    p.help="Automatic prepares expanded, connected or referenced helpers. All Available also serves direct buffer access and scripts.";
    const char* auxNames[]={"automatic","all"};const char* auxLabels[]={"Automatic","All Available"};check(m->appendMenu(p,2,auxNames,auxLabels));
    TD::OP_NumericParameter n;n.page="Source";n.name="Autorefresh";n.label="Auto Refresh";n.defaultValues[0]=1;n.section=true;check(m->appendToggle(n));
    n=TD::OP_NumericParameter("Refresh");n.page="Source";n.label="Refresh";check(m->appendPulse(n));
    n=TD::OP_NumericParameter("Checkinterval");n.page="Technical";n.label="Check Interval (s)";n.defaultValues[0]=1;
    n.minValues[0]=n.minSliders[0]=.1;n.maxValues[0]=86400;n.maxSliders[0]=10;n.clampMins[0]=n.clampMaxes[0]=true;check(m->appendFloat(n));
    n=TD::OP_NumericParameter("Repairoutputs");n.page="Technical";n.label="Create/Repair Docked Outputs";check(m->appendPulse(n));
}
void AppleImageFileIn::setParameterEnableStates(const TD::OP_Inputs* inputs,TD::OP_ParEnableState* states,void*) {
    states->setEnableState("Checkinterval",inputs->getParInt("Autorefresh")!=0);
    states->setEnableState("Image",published_->images.size()>1);
}
void AppleImageFileIn::buildDynamicMenu(const TD::OP_Inputs*,TD::OP_BuildDynamicMenuInfo* info,void*) {
    if(std::strcmp(info->name,"Image"))return;
    info->addMenuEntry("primary","Primary Image");
    for(auto& image:published_->images)info->addMenuEntry(std::to_string(image.index).c_str(),image.label.c_str());
}
void AppleImageFileIn::pulsePressed(const char* name,void*) {
    if(!std::strcmp(name,"Refresh"))refresh_=true;
    if(!std::strcmp(name,"Repairoutputs"))dockWarning_=host_.schedule(true);
}
uint32_t AppleImageFileIn::availableMask() const {uint32_t mask=0;for(size_t i=0;i<BufferCount;++i)if(published_->planes[i].available())mask|=1u<<i;return mask;}
void AppleImageFileIn::upload(TD::TOP_Output* output,const TD::OP_Inputs* inputs,const Result& result) {
    namespace Packing=TDPlugin::TOPOutput;
    auto primary=colorDesc(output,inputs,result.planes[0]);
    const bool unmanaged=node_->workingColorSpace==TD::OP_WorkingColorSpace::Passthrough;
    const uint32_t requested=std::string(parameter(inputs,"Auxiliaryoutputs","automatic"))=="all"?AllAuxiliaries:demandMask_;
    std::array<std::string,BufferCount> signatures;
    uint32_t active=0;
    std::array<TD::OP_SmartRef<TD::TOP_Buffer>,BufferCount> buffers;
    std::array<TD::TOP_UploadInfo,BufferCount> infos;
    // Allocate/fill everything before submitting any texture; allocations cannot
    // leave a new color image paired with old metadata or auxiliary maps.
    for(size_t slot=0;slot<BufferCount;++slot) {
        const auto& p=result.planes[slot];auto& info=infos[slot];auto& d=info.textureDesc;
        const bool enabled=p.present() && (!slot || (requested&(1u<<slot)));
        if(enabled)active|=1u<<slot;
        if(!slot)d=primary;
        else {d.width=enabled?std::min(p.width,primary.width):1;d.height=enabled?std::min(p.height,primary.height):1;d.depth=1;d.texDim=TD::OP_TexDim::e2D;
            d.aspectX=float(d.width);d.aspectY=float(d.height);
            d.pixelFormat=!enabled?TD::OP_PixelFormat::Mono32Float:p.channels==2?TD::OP_PixelFormat::RG32Float:p.channels>=3?TD::OP_PixelFormat::RGBA32Float:TD::OP_PixelFormat::Mono32Float;}
        std::ostringstream signature;
        signature<<(enabled?result.sourceRevision:0)<<':'<<enabled<<':'<<d.width<<':'<<d.height<<':'<<int(d.pixelFormat)<<':'<<d.aspectX<<':'<<d.aspectY;
        if(!slot)signature<<':'<<int(node_->workingColorSpace);
        signatures[slot]=signature.str();
        if(signatures[slot]==uploadSignatures_[slot])continue;
        imageElements(d.width,d.height,4);
        size_t bpp=Packing::bytesPerPixel(d.pixelFormat),bytes=size_t(d.width)*d.height*bpp;
        if(!bpp||bytes>MaxImageBytes)throw std::runtime_error("Requested output exceeds the 1 GiB buffer limit");
        buffers[slot]=context_->createOutputBuffer(bytes,TD::TOP_BufferFlags::None,nullptr);
        auto& buffer=buffers[slot];if(!buffer||!buffer->data||buffer->size<bytes)throw std::runtime_error("Could not allocate image output buffer");
        uint8_t* out=static_cast<uint8_t*>(buffer->data);
        if(!enabled)std::memset(out,0,bytes);
        else {
            if(p.pixels->size()!=imageElements(p.width,p.height,p.channels))throw std::runtime_error("Decoder returned invalid image storage");
            for(uint32_t y=0;y<d.height;++y)for(uint32_t x=0;x<d.width;++x) {
                float v[4]={0,0,0,1};
                for(uint32_t c=0;c<p.channels;++c)v[c]=sample(p,x,y,d.width,d.height,c);
                if(p.channels==1)v[1]=v[2]=v[0];
                if(!slot&&unmanaged) {
                    // Passthrough projects ignore upload color-space conversion.
                    // Supply display-referred sRGB there, including the transfer
                    // function, instead of displaying linear light as sRGB.
                    for(int c=0;c<3;++c) {
                        float straight=v[3]>0?v[c]/v[3]:0;
                        float a=std::fabs(straight);
                        float encoded=a<=.0031308f?12.92f*a:1.055f*std::pow(a,1.f/2.4f)-.055f;
                        v[c]=std::copysign(encoded,straight)*v[3];
                    }
                }
                Packing::packPixel(out,d.pixelFormat,v[0],v[1],v[2],v[3]);
            }
        }
        info.firstPixel=TD::TOP_FirstPixel::BottomLeft;info.colorBufferIndex=uint32_t(slot);
        info.colorSpace=slot?TD::OP_ColorSpace::Passthrough:unmanaged?TD::OP_ColorSpace::SRGB:TD::OP_ColorSpace::SRGBLinear;
        info.referenceWnite=TD::OP_ReferenceWhite::SDR;
    }
    for(size_t slot=0;slot<BufferCount;++slot)if(buffers[slot]) {
        output->uploadBuffer(&buffers[slot],infos[slot],nullptr);
        if(slot && (active&(1u<<slot)))++auxiliaryUploads_;
    }
    uploadSignatures_=std::move(signatures);activeMask_=active; colorDesc_=primary;uploadedWorkingSpace_=node_->workingColorSpace;
}
void AppleImageFileIn::execute(TD::TOP_Output* output,const TD::OP_Inputs* inputs,void*) {
    try {
        Request r;r.path=host_.resolve();r.image=parameter(inputs,"Image","primary");r.decode=parameter(inputs,"Decode","auto");
        r.automatic=inputs->getParInt("Autorefresh")!=0;r.interval=inputs->getParDouble("Checkinterval");
        r.auxiliaryMask=std::string(parameter(inputs,"Auxiliaryoutputs","automatic"))=="all"?AllAuxiliaries:demandMask_;
        engine_.request(r,refresh_);refresh_=false;auto next=engine_.result();
        auto before=uploadSignatures_;upload(output,inputs,*next);
        bool changed=revision_!=next->revision;
        published_=next;error_=next->error;warning_.clear();
        for(auto& w:next->warnings){if(!warning_.empty())warning_+='\n';warning_+=w;}
        if(changed||before!=uploadSignatures_||publishedDemand_!=demandMask_) {
            Result metadata=*next;Table table(metadata);
            for(size_t slot=1;slot<BufferCount;++slot)if(next->planes[slot].available()) {
                const std::string key=std::string("outputs.")+Kinds[slot];
                table.add(key+".native_width",std::to_string(next->planes[slot].width),"Output","Plugin");
                table.add(key+".native_height",std::to_string(next->planes[slot].height),"Output","Plugin");
                table.add(key+".active",activeMask_&(1u<<slot)?"true":"false","Output","Plugin");
                table.add(key+".state",!(r.auxiliaryMask&(1u<<slot))?"inactive":next->planes[slot].present()?"ready":"loading","Output","Plugin");
            }
            if(next->planes[0].present()) {
                table.add("decode.auxiliary_preparations",std::to_string(next->preparations),"Decode","Plugin");
                table.add("decode.auxiliary_prepare_ms",std::to_string(next->prepareMs),"Decode","Plugin");
            }
            table.finish();rows_=std::move(metadata.rows);
            for(auto& row:rows_) {
                if(row[0].compare(0,8,"outputs."))continue;
                if(row[0]=="outputs.color.width")row[1]=std::to_string(colorDesc_.width);
                if(row[0]=="outputs.color.height")row[1]=std::to_string(colorDesc_.height);
                for(size_t slot=1;slot<BufferCount;++slot) {
                    const std::string key=std::string("outputs.")+Kinds[slot];
                    if(row[0]==key+".width")row[1]=std::to_string(std::min(next->planes[slot].width,colorDesc_.width));
                    if(row[0]==key+".height")row[1]=std::to_string(std::min(next->planes[slot].height,colorDesc_.height));
                }
            }
            revision_=next->revision;publishedDemand_=demandMask_;
            dockWarning_=host_.schedule(false,initial_);initial_=false;
        }
    } catch(const std::exception& e) {
        error_=cleanUTF8(e.what());warning_.clear();rows_.clear();
        engine_.request(Request{});
        auto empty=std::make_shared<Result>();empty->status="error";empty->error=error_;empty->revision=UINT64_MAX;
        published_=empty;revision_=UINT64_MAX;uploadSignatures_={};
        try {upload(output,inputs,*empty);}catch(...) {error_+="; host could not clear all output textures";}
        dockWarning_=host_.schedule();
    } catch(...) {error_="Unexpected Image File In host failure";rows_.clear();uploadSignatures_={};}
}
void AppleImageFileIn::getErrorString(TD::OP_String* s,void*){s->setString(error_.c_str());}
void AppleImageFileIn::getWarningString(TD::OP_String* s,void*){auto w=warning_;if(!dockWarning_.empty()){if(!w.empty())w+='\n';w+=dockWarning_;}s->setString(w.c_str());}
void AppleImageFileIn::getInfoPopupString(TD::OP_String* s,void*) {
    std::ostringstream out;out<<"Apple Image In 0.2.0\nStatus: "<<(engine_.reading()?"reading":published_->status)<<"\nFile: "<<published_->path
        <<"\nImages: "<<published_->images.size()<<"; selected: "<<published_->selected<<"\nReads: "<<published_->reads<<"; read ms: "<<published_->readMs
        <<"\nAuxiliary preparations: "<<published_->preparations<<"; uploads: "<<auxiliaryUploads_<<"; prepare ms: "<<published_->prepareMs
        <<"\nColor: "<<colorDesc_.width<<" x "<<colorDesc_.height<<"\n"<<warning_<<"\n"<<dockWarning_<<"\n"<<error_;s->setString(out.str().c_str());
}
bool AppleImageFileIn::getInfoDATSize(TD::OP_InfoDATSize* size,void*){size->rows=int32_t(rows_.size()+1);size->cols=4;size->byColumn=false;return true;}
void AppleImageFileIn::getInfoDATEntries(int32_t row,int32_t count,TD::OP_InfoDATEntries* entries,void*) {
    static const char* header[]={"key","value","group","source"};if(row<0||size_t(row)>rows_.size())return;
    for(int col=0;col<std::min(count,4);++col)entries->values[col]->setString(row?rows_[size_t(row-1)][col].c_str():header[col]);
}
void AppleImageFileIn::getInfoCHOPChan(int32_t index,TD::OP_InfoCHOPChan* channel,void*) {
    static const char* names[]={"reading","images","image_index","width","height","read_ms"};
    if(index<0||index>=29)return;
    if(index<6){channel->name->setString(names[index]);const double values[]={engine_.reading()?1.:0.,double(published_->images.size()),double(published_->selected),double(colorDesc_.width),double(colorDesc_.height),published_->readMs};channel->value=float(values[index]);}
    else if(index<16) {size_t slot=size_t(index-5);auto name=std::string("has_")+Kinds[slot];channel->name->setString(name.c_str());channel->value=published_->planes[slot].available()?1:0;}
    else if(index<26){size_t slot=size_t(index-15);auto name=std::string("active_")+Kinds[slot];channel->name->setString(name.c_str());channel->value=activeMask_&(1u<<slot)?1:0;}
    else {const char* names[]={"auxiliary_preparations","auxiliary_uploads","auxiliary_prepare_ms"};const double values[]={double(published_->preparations),double(auxiliaryUploads_),published_->prepareMs};channel->name->setString(names[index-26]);channel->value=float(values[index-26]);}
}
