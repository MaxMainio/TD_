#include "HostStubs.h"
#include "SegmentUV.h"
#include "SegmentEngine.h"
#include "OutputPacking.h"
#include "Docking.h"
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace TD;
static int schedules = 0;
static bool repaired = false;
namespace Segment
{
// The Python bridge is tested separately. No Python runtime in this C++ test.
void configureDocking(OP_CustomOPInfo& info) { info.cookOnStart = true; }
std::string scheduleDocking(uint32_t nodeId, bool reset)
{ if (nodeId != 17) throw std::runtime_error("wrong owner id"); ++schedules; repaired = reset; return {}; }
}
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

template<class Base> class Ref : public Base
{
    int refs_ = 1;
public:
    void acquire() override { ++refs_; }
    void release() override { if (!--refs_) delete this; }
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
    explicit Buffer(size_t count) : bytes(count) { data=bytes.data(); size=count; }
};
class Download : public Ref<OP_TOPDownloadResult>
{
public:
    cv::Mat image;
    bool nullData = false;
    void* getData() override { return nullData ? nullptr : image.data; }
};
class Source : public TestHost::InputBase
{
public:
    cv::Mat image;
    mutable int downloads = 0;
    int failure = 0;
    Source()
    {
        image=cv::Mat(13,23,CV_32FC4,cv::Scalar(0));
        for (int y=1;y<11;++y) for (int x=2;x<21;++x)
            if((x<7 && y<9) || (x>13 && y>4)) image.at<cv::Vec4f>(y,x)[3]=1;
        textureDesc.width=image.cols; textureDesc.height=image.rows;
        textureDesc.texDim=OP_TexDim::e2D; textureDesc.pixelFormat=OP_PixelFormat::RGBA32Float;
    }
    OP_SmartRef<OP_TOPDownloadResult> downloadTexture(const OP_TOPInputDownloadOptions& opts, void*) const override
    {
        ++downloads;
        require(opts.verticalFlip && opts.pixelFormat==OP_PixelFormat::RGBA32Float,"input download convention");
        require(opts.colorSpace==OP_ColorSpace::DefaultForWorkingColorSpace,"input color space changed");
        OP_SmartRef<OP_TOPDownloadResult> ref;
        if (failure==1) return ref;
        auto* result=new Download;
        result->image=image.clone();
        result->textureDesc=textureDesc;
        result->textureDesc.pixelFormat=OP_PixelFormat::RGBA32Float;
        result->size=image.total()*16;
        if(failure==2) result->nullData=true;
        if(failure==3) result->size-=1;
        if(failure==4) result->textureDesc.pixelFormat=OP_PixelFormat::RGBA8Fixed;
        if(failure==5) result->textureDesc.width=0;
        ref.takeOwnership(result);
        return ref;
    }
};
class Context : public TestHost::ContextBase
{
public:
    int failure=0;
    OP_SmartRef<TOP_Buffer> createOutputBuffer(uint64_t size, TOP_BufferFlags, void*) override
    {
        OP_SmartRef<TOP_Buffer> ref;
        if(failure==1) return ref;
        auto* buffer=new Buffer(size);
        if(failure==2) buffer->data=nullptr;
        if(failure==3) buffer->size=size-1;
        ref.takeOwnership(buffer);
        return ref;
    }
};
class Output : public TestHost::OutputBase
{
public:
    OP_TextureDesc suggested, uploaded;
    std::vector<uint8_t> bytes;
    bool fail=false;
    std::function<void()> duringUpload;
    void getSuggestedOutputDesc(OP_TextureDesc* desc, void*) override { *desc=suggested; }
    void uploadBuffer(OP_SmartRef<TOP_Buffer>* buffer, const TOP_UploadInfo& info, void*) override
    {
        if(duringUpload) duringUpload();
        if(fail) throw std::runtime_error("simulated upload failure");
        require(info.firstPixel==TOP_FirstPixel::BottomLeft && info.colorBufferIndex==0,"upload convention");
        auto* data=static_cast<uint8_t*>((*buffer)->data);
        bytes.assign(data,data+(*buffer)->size);
        uploaded=info.textureDesc;
        buffer->release();
    }
};
class String : public OP_String
{
public:
    std::string text;
    void setString(const char* value) override { text=value; }
};
std::vector<std::string> table(SegmentUV& plugin)
{
    OP_InfoDATSize size;
    require(plugin.getInfoDATSize(&size,nullptr) && !size.byColumn,"DAT interface");
    std::vector<std::string> result;
    for(int row=0;row<size.rows;++row)
    {
        String values[15]; OP_String* pointers[15];
        for(int col=0;col<size.cols;++col) pointers[col]=&values[col];
        OP_InfoDATEntries entries; entries.values=pointers;
        plugin.getInfoDATEntries(row,size.cols,&entries,nullptr);
        for(int col=0;col<size.cols;++col) result.push_back(values[col].text);
    }
    return result;
}
int main()
{
    try
    {
        Context context; Source source; Output output; TestHost::Inputs inputs;
        inputs.source=&source;
        OP_NodeInfo node; node.opId=17;
        SegmentUV plugin(&node,&context);
        require(schedules==1,"one initial docking request");
        output.suggested=source.textureDesc;
        output.suggested.aspectX=16; output.suggested.aspectY=9;
        TOP_GeneralInfo general;
        plugin.getGeneralInfo(&general,&inputs,nullptr);
        require(!general.cookEveryFrame && !general.cookEveryFrameIfAsked && general.inputSizeIndex==0,"demand scheduling");
        const OP_PixelFormat formats[]={OP_PixelFormat::RGBA8Fixed,OP_PixelFormat::BGRA8Fixed,
            OP_PixelFormat::RGBA16Fixed,OP_PixelFormat::RGBA16Float,OP_PixelFormat::RGBA32Float,
            OP_PixelFormat::Mono8Fixed,OP_PixelFormat::Mono16Fixed,OP_PixelFormat::Mono16Float,OP_PixelFormat::Mono32Float,
            OP_PixelFormat::RG8Fixed,OP_PixelFormat::RG16Fixed,OP_PixelFormat::RG16Float,OP_PixelFormat::RG32Float,
            OP_PixelFormat::A8Fixed,OP_PixelFormat::A16Fixed,OP_PixelFormat::A16Float,OP_PixelFormat::A32Float,
            OP_PixelFormat::MonoA8Fixed,OP_PixelFormat::MonoA16Fixed,OP_PixelFormat::MonoA16Float,OP_PixelFormat::MonoA32Float,
            OP_PixelFormat::RGB10A2Fixed};
        size_t comparisons=0;
        for(int method=0;method<5;++method) for(int detail=0;detail<3;++detail)
        {
            inputs.method=method; inputs.detail=detail;
            const auto image=Segment::process(source.image,.5,MethodMenuItems(method),1);
            std::vector<std::string> snapshot;
            for(auto format:formats) for(bool useInput:{false,true}) for(auto dims:{cv::Size(23,13),cv::Size(7,5),cv::Size(39,24)})
            {
                inputs.useInput=useInput;
                source.textureDesc.pixelFormat=useInput?format:OP_PixelFormat::RGBA32Float;
                output.suggested.pixelFormat=useInput?OP_PixelFormat::RGBA32Float:format;
                output.suggested.width=dims.width; output.suggested.height=dims.height;
                plugin.execute(&output,&inputs,nullptr);
                String error; plugin.getErrorString(&error,nullptr); require(error.text.empty(),"unexpected cook error");
                require(output.uploaded.pixelFormat==format && output.uploaded.width==unsigned(dims.width)
                    && output.uploaded.height==unsigned(dims.height),"Common format/resolution");
                require(output.uploaded.aspectX==16 && output.uploaded.aspectY==9,"Common aspect");
                cv::Mat expected=image.clone();
                if(expected.size()!=dims) cv::resize(expected,expected,dims);
                cv::flip(expected,expected,0);
                auto packed=TDPlugin::TOPOutput::createPackedRGBA32Buffer(&context,expected.ptr<float>(),
                    uint32_t(expected.step),output.uploaded);
                require(packed->size==output.bytes.size() && !std::memcmp(packed->data,output.bytes.data(),packed->size),"legacy resize/flip/packing parity");
                const auto current=table(plugin);
                if(snapshot.empty()) snapshot=current;
                require(snapshot==current,"Common settings changed segment measurements");
                ++comparisons;
            }
        }
        inputs.detail=2;
        const auto good=[&]() {
            inputs.source=&source; source.failure=0; context.failure=0; output.fail=false;
            plugin.execute(&output,&inputs,nullptr);
            require(table(plugin).size()==45,"recovery rows");
        };
        const auto bad=[&]() {
            plugin.execute(&output,&inputs,nullptr);
            require(table(plugin).size()==15,"failed frame retained rows");
            String a,b; plugin.getErrorString(&a,nullptr); plugin.getErrorString(&b,nullptr);
            require(!a.text.empty() && a.text==b.text,"persistent error");
        };
        good(); inputs.source=nullptr; bad(); good();
        for(int fail=1;fail<=5;++fail) { source.failure=fail; bad(); good(); }
        for(int fail=1;fail<=3;++fail) { context.failure=fail; bad(); good(); }
        output.fail=true; bad(); good();
        source.textureDesc.texDim=OP_TexDim::e3D; bad(); source.textureDesc.texDim=OP_TexDim::e2D; good();
        inputs.source=nullptr; inputs.detail=1;
        plugin.getGeneralInfo(&general,&inputs,nullptr);
        require(table(plugin).size()==9,"disconnect/schema before execute");
        inputs.detail=2; good();
        const int downloads=source.downloads;
        for(int i=0;i<100;++i) table(plugin);
        require(source.downloads==downloads && schedules==1,"DAT read repeated image work/docking");
        output.duringUpload=[&]() { require(table(plugin).size()==15,"published rows before upload completion"); };
        good(); output.duringUpload={};
        source.image.setTo(cv::Scalar(0));
        plugin.execute(&output,&inputs,nullptr);
        require(table(plugin).size()==15,"empty mask stale rows");
        plugin.pulsePressed("Repairsegmentdat",nullptr);
        require(schedules==2 && repaired,"repair pulse");
        std::cout << comparisons << " Common output parity checks; cached reads, scheduling flags, upload publication, failures and recovery passed\n";
    }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
