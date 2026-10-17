#include "HostStubs.h"
#include "AppleFaceDetection.h"
#include "TOPOutputHelper.h"
#include "Docking.h"
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace TD;
void require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
static int schedules=0, detections=0;
static bool repaired=false, failDetection=false;
static std::vector<FaceDetection::Face> detected;
namespace FaceDetection
{
// Real Vision and Python are exercised separately. These tests inject detections
// and failures so every host-output path is deterministic and needs no photos.
struct VisionDetector::Impl {};
VisionDetector::VisionDetector():impl_(new Impl) {}
VisionDetector::~VisionDetector()=default;
std::vector<Face> VisionDetector::detect(const uint8_t* pixels,size_t bytes,uint32_t width,uint32_t height)
{
    ++detections;
    require(pixels && bytes==size_t(width)*height*4,"Vision input dimensions");
    require(pixels[0]==42,"downloaded pixels reach Vision");
    if(failDetection) throw std::runtime_error("simulated Vision failure");
    return detected;
}
void configureDocking(OP_CustomOPInfo& info) { info.cookOnStart=true; }
std::string scheduleDocking(uint32_t nodeId,bool reset)
{ require(nodeId==17,"docking node ID"); ++schedules; repaired=reset; return {}; }
}
template<class Base> class Ref : public Base
{
    int refs_=1;
public:
    void acquire() override { ++refs_; }
    void release() override { if(!--refs_) delete this; }
    void reserved0() override {}
    void reserved1() override {}
    void reserved2() override {}
    void reserved3() override {}
    void reserved4() override {}
};
class Buffer : public Ref<TOP_Buffer>
{
public:
    std::vector<uint8_t> bytes;
    explicit Buffer(size_t count):bytes(count) { data=bytes.data(); size=count; }
};
class Download : public Ref<OP_TOPDownloadResult>
{
public:
    std::vector<uint8_t> bytes;
    bool nullData=false;
    void* getData() override { return nullData?nullptr:bytes.data(); }
};
class Source : public TestHost::InputBase
{
public:
    int failure=0;
    Source()
    {
        textureDesc.width=8; textureDesc.height=6;
        textureDesc.texDim=OP_TexDim::e2D; textureDesc.pixelFormat=OP_PixelFormat::RGBA8Fixed;
    }
    OP_SmartRef<OP_TOPDownloadResult> downloadTexture(const OP_TOPInputDownloadOptions& options,void*) const override
    {
        require(options.verticalFlip && options.pixelFormat==OP_PixelFormat::BGRA8Fixed,"Vision download convention");
        require(options.colorSpace==OP_ColorSpace::SRGB,"Vision expects sRGB pixels");
        OP_SmartRef<OP_TOPDownloadResult> result;
        if(failure==1) return result;
        auto* d=new Download;
        d->bytes.assign(8*6*4,42); d->size=d->bytes.size(); d->textureDesc=textureDesc;
        d->textureDesc.pixelFormat=OP_PixelFormat::BGRA8Fixed;
        if(failure==2) d->nullData=true;
        if(failure==3) d->size-=1;
        if(failure==4) d->textureDesc.pixelFormat=OP_PixelFormat::RGBA8Fixed;
        if(failure==5) d->textureDesc.width=0;
        result.takeOwnership(d); return result;
    }
};
class Inputs : public TestHost::Inputs
{
public:
    int shape=2, selection=0;
    int32_t getParInt(const char* name,int32_t=0) const override
    { return std::string(name)=="Maskshape"?shape:selection; }
};
class Context : public TestHost::ContextBase
{
public:
    int failure=0;
    OP_SmartRef<TOP_Buffer> createOutputBuffer(uint64_t size,TOP_BufferFlags,void*) override
    {
        OP_SmartRef<TOP_Buffer> result;
        if(failure==1) return result;
        auto* b=new Buffer(size);
        if(failure==2) b->data=nullptr;
        if(failure==3) b->size=size-1;
        result.takeOwnership(b); return result;
    }
};
class Output : public TestHost::OutputBase
{
public:
    OP_TextureDesc suggested,uploaded;
    std::vector<uint8_t> bytes;
    bool fail=false;
    std::function<void()> duringUpload;
    void getSuggestedOutputDesc(OP_TextureDesc* d,void*) override { *d=suggested; }
    void uploadBuffer(OP_SmartRef<TOP_Buffer>* b,const TOP_UploadInfo& info,void*) override
    {
        if(duringUpload) duringUpload();
        if(fail) throw std::runtime_error("simulated upload failure");
        require(info.firstPixel==TOP_FirstPixel::BottomLeft && info.colorBufferIndex==0,"mask upload origin");
        require(info.colorSpace==OP_ColorSpace::Passthrough,"mask values must not receive a color transform");
        auto* data=static_cast<const uint8_t*>((*b)->data);
        bytes.assign(data,data+(*b)->size); uploaded=info.textureDesc; b->release();
    }
};
class String : public OP_String
{
public:
    std::string text;
    void setString(const char* s) override { text=s; }
};
std::vector<std::string> table(AppleFaceDetection& plugin)
{
    OP_InfoDATSize size;
    require(plugin.getInfoDATSize(&size,nullptr) && !size.byColumn && size.cols==10,"DAT schema");
    std::vector<std::string> result;
    for(int row=0;row<size.rows;++row)
    {
        String cells[10]; OP_String* pointers[10];
        for(int c=0;c<10;++c) pointers[c]=&cells[c];
        OP_InfoDATEntries entries; entries.values=pointers;
        plugin.getInfoDATEntries(row,10,&entries,nullptr);
        for(auto& cell:cells) result.push_back(cell.text);
    }
    return result;
}
int main()
{
    try
    {
        Context context; Source source; Output output; Inputs inputs;
        inputs.source=&source; inputs.useInput=false;
        OP_NodeInfo node; node.opId=17;
        AppleFaceDetection plugin(&node,&context);
        require(schedules==1,"initial docking request");
        FaceDetection::Face face;
        face.box={0,0,.5,.5}; face.confidence=.9;
        face.landmarks={{"nose",0,{.25,.25}}}; detected={face};
        output.suggested=source.textureDesc;
        output.suggested.aspectX=16; output.suggested.aspectY=9;
        TOP_GeneralInfo general;
        plugin.getGeneralInfo(&general,&inputs,nullptr);
        require(!general.cookEveryFrame && !general.cookEveryFrameIfAsked && general.inputSizeIndex==0,"demand cooking");
        const OP_PixelFormat formats[]={OP_PixelFormat::BGRA8Fixed,OP_PixelFormat::RGBA8Fixed,
            OP_PixelFormat::RGBA16Fixed,OP_PixelFormat::RGBA16Float,OP_PixelFormat::RGBA32Float,
            OP_PixelFormat::Mono8Fixed,OP_PixelFormat::Mono16Fixed,OP_PixelFormat::Mono16Float,OP_PixelFormat::Mono32Float,
            OP_PixelFormat::RG8Fixed,OP_PixelFormat::RG16Fixed,OP_PixelFormat::RG16Float,OP_PixelFormat::RG32Float,
            OP_PixelFormat::A8Fixed,OP_PixelFormat::A16Fixed,OP_PixelFormat::A16Float,OP_PixelFormat::A32Float,
            OP_PixelFormat::MonoA8Fixed,OP_PixelFormat::MonoA16Fixed,OP_PixelFormat::MonoA16Float,OP_PixelFormat::MonoA32Float,
            OP_PixelFormat::RGB10A2Fixed,OP_PixelFormat::RGB11Float};
        int checks=0;
        for(auto format:formats) for(bool useInput:{false,true}) for(uint32_t scale:{1,2})
        {
            inputs.useInput=useInput; source.textureDesc.pixelFormat=useInput?format:OP_PixelFormat::RGBA8Fixed;
            output.suggested.pixelFormat=useInput?OP_PixelFormat::RGBA8Fixed:format;
            output.suggested.width=8*scale; output.suggested.height=6*scale;
            plugin.execute(&output,&inputs,nullptr);
            String error; plugin.getErrorString(&error,nullptr); require(error.text.empty(),"successful cook");
            require(output.uploaded.pixelFormat==format && output.uploaded.width==8*scale && output.uploaded.height==6*scale,"Common format and dimensions");
            require(output.uploaded.aspectX==16 && output.uploaded.aspectY==9,"Common aspect");
            require(table(plugin).size()==20 && table(plugin)[13]=="0.25" && table(plugin)[14]=="0.25","landmarks independent of Common size");
            const auto bpp=TDPlugin::TOPOutput::bytesPerPixel(format);
            require(output.bytes.size()==size_t(8*scale)*6*scale*bpp,"output buffer size");
            for(uint32_t y=0;y<6*scale;++y) for(uint32_t x=0;x<8*scale;++x)
            {
                const auto offset=(size_t(y)*8*scale+x)*bpp;
                if(x>=4*scale || y>=3*scale)
                    for(uint32_t c=0;c<bpp;++c) require(output.bytes[offset+c]==0,"background must be zero, including alpha");
            }
            if(format==OP_PixelFormat::RGBA8Fixed)
                require(output.bytes[0]==255 && output.bytes[3]==255,"mask is white with mask alpha");
            if(format==OP_PixelFormat::Mono32Float)
            { float value; std::memcpy(&value,output.bytes.data(),sizeof(value)); require(value==1,"float mask value"); }
            ++checks;
        }
        const auto good=[&](){
            inputs.source=&source; source.failure=context.failure=0; output.fail=failDetection=false;
            plugin.execute(&output,&inputs,nullptr); require(table(plugin).size()==20,"recovery landmark rows");
        };
        const auto bad=[&](){
            plugin.execute(&output,&inputs,nullptr); require(table(plugin).size()==10,"failure clears landmarks");
            String error; plugin.getErrorString(&error,nullptr); require(!error.text.empty(),"failure reported");
        };
        good(); inputs.source=nullptr; bad(); good();
        for(int i=1;i<=5;++i) { source.failure=i; bad(); good(); }
        for(int i=1;i<=3;++i) { context.failure=i; bad(); good(); }
        failDetection=true; bad();
        for(auto byte:output.bytes) require(byte==0,"Vision failure clears output");
        good(); output.fail=true; bad(); good();
        source.textureDesc.texDim=OP_TexDim::e3D; bad(); source.textureDesc.texDim=OP_TexDim::e2D; good();
        output.duringUpload=[&](){ require(table(plugin).size()==10,"DAT published before matching upload"); };
        good(); output.duringUpload={};
        const int before=detections;
        for(int i=0;i<100;++i) table(plugin);
        require(detections==before && schedules==1,"DAT reads repeat no detection or docking");
        detected.clear(); plugin.execute(&output,&inputs,nullptr);
        require(table(plugin).size()==10,"no-face frame clears landmarks");
        for(auto byte:output.bytes) require(byte==0,"no-face frame clears mask");
        detected={face}; good(); inputs.source=nullptr;
        plugin.getGeneralInfo(&general,&inputs,nullptr); require(table(plugin).size()==10,"disconnect clears DAT before execute");
        plugin.pulsePressed("Repairlandmarksdat",nullptr); require(schedules==2 && repaired,"repair pulse");
        std::cout<<checks<<" Common output checks; cached DAT reads, failures, no-face frames, recovery and publication order passed\n";
    }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
