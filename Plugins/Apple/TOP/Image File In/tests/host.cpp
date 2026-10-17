#include "HostStubs.h"
#include "AppleImageFileIn.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <cmath>
using namespace TD;
void require(bool v,const char* m){if(!v)throw std::runtime_error(m);}
static std::string path;static int scheduled=0;
namespace ImageFile {
void configurePython(OP_CustomOPInfo&){}
Host::~Host()=default;
std::string Host::resolve(){return path;}
std::string Host::schedule(bool,bool){++scheduled;return {};}
}
template<class Base>class Ref:public Base{int refs=1;public:
void acquire()override{++refs;}void release()override{if(!--refs)delete this;}
void reserved0()override{}void reserved1()override{}void reserved2()override{}void reserved3()override{}void reserved4()override{}};
class Buffer:public Ref<TOP_Buffer>{public:std::vector<uint8_t> bytes;Buffer(size_t n):bytes(n){data=bytes.data();size=n;}};
class Context:public TestHost::ContextBase{public:bool fail=false;
OP_SmartRef<TOP_Buffer> createOutputBuffer(uint64_t n,TOP_BufferFlags,void*)override{OP_SmartRef<TOP_Buffer> b;if(!fail)b.takeOwnership(new Buffer(n));return b;}};
class Output:public TestHost::OutputBase{public:
std::array<std::vector<uint8_t>,11> pixels;std::array<TOP_UploadInfo,11> infos;OP_TextureDesc suggested;int uploads=0;
void getSuggestedOutputDesc(OP_TextureDesc* d,void*)override{*d=suggested;}
void uploadBuffer(OP_SmartRef<TOP_Buffer>* b,const TOP_UploadInfo& info,void*)override{
require(info.colorBufferIndex<11,"Buffer range");auto i=info.colorBufferIndex;infos[i]=info;auto* p=(uint8_t*)(*b)->data;pixels[i].assign(p,p+(*b)->size);++uploads;b->release();}};
class Inputs:public TestHost::Inputs{public:std::string image="primary",format="useinput",resolution="useinput";int boundWidth=4,boundHeight=4;
int32_t getParInt(const char* n,int32_t index=0)const override{return !std::strcmp(n,"resolution")?(index?boundHeight:boundWidth):0;}
const char* getParString(const char* n)const override{if(!std::strcmp(n,"Image"))return image.c_str();if(!std::strcmp(n,"format"))return format.c_str();if(!std::strcmp(n,"outputresolution"))return resolution.c_str();return "auto";}};
class String:public OP_String{public:std::string value;void setString(const char* s)override{value=s;}};
int main(int argc,char** argv){try{
require(argc==2,"Fixtures required");std::filesystem::path root=argv[1];Context context;Output output;Inputs inputs;OP_NodeInfo node{};node.opId=17;node.workingColorSpace=OP_WorkingColorSpace::SRGBLinear;AppleImageFileIn plugin(&node,&context);
auto wait=[&](const char* status){for(int i=0;i<500;++i){plugin.execute(&output,&inputs,nullptr);if(plugin.snapshot()->status==status)return;std::this_thread::sleep_for(std::chrono::milliseconds(5));}String error;plugin.getErrorString(&error,nullptr);throw std::runtime_error("Host result timeout: "+error.value);};
path=(root/"portrait.heic").string();wait("ready");
require(plugin.availableMask()&4,"Disparity availability published while inactive");
require(plugin.snapshot()->preparations==0&&!plugin.snapshot()->planes[2].present(),"Collapsed helpers skip float preparation");
require(output.infos[2].textureDesc.width==1,"Inactive auxiliary is a cleared placeholder");
auto colorStorage=plugin.snapshot()->planes[0].pixels;auto reads=plugin.snapshot()->reads;
plugin.setDemand(4);
for(int i=0;i<500&&!plugin.snapshot()->planes[2].present();++i){plugin.execute(&output,&inputs,nullptr);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
require(plugin.snapshot()->planes[2].present()&&plugin.snapshot()->preparations==1,"Only demanded auxiliary prepared");
require(plugin.snapshot()->reads==reads&&plugin.snapshot()->planes[0].pixels==colorStorage,"Resume uses same source snapshot without rereading or copying color");
require(plugin.snapshot()->planes[3].available()&&!plugin.snapshot()->planes[3].present(),"Other available auxiliaries stay unprepared");
int preparedUploads=output.uploads;plugin.setDemand(0);plugin.execute(&output,&inputs,nullptr);
require(output.uploads==preparedUploads+1&&output.infos[2].textureDesc.width==1,"Suspend clears only previously active auxiliary");
plugin.setDemand(4);plugin.execute(&output,&inputs,nullptr);
require(output.uploads==preparedUploads+2&&plugin.snapshot()->preparations==1,"Resume reuses prepared cache and uploads only that auxiliary");
require(output.infos[0].textureDesc.width==8,"Native color resolution");require(output.infos[2].textureDesc.width==2,"Native auxiliary resolution");
require(output.infos[0].textureDesc.pixelFormat==OP_PixelFormat::RGBA16Float,"Default color format");require(output.infos[2].textureDesc.pixelFormat==OP_PixelFormat::Mono32Float,"Depth float format");
require(output.infos[2].colorSpace==OP_ColorSpace::Passthrough&&output.infos[0].colorSpace==OP_ColorSpace::SRGBLinear,"Color/data separation");
int before=output.uploads;for(int i=0;i<30;++i)plugin.execute(&output,&inputs,nullptr);require(output.uploads==before,"Static images not uploaded every frame");
inputs.resolution="custom";inputs.format="rgba32float";output.suggested.width=20;output.suggested.height=30;output.suggested.pixelFormat=OP_PixelFormat::RGBA32Float;
before=output.uploads;plugin.execute(&output,&inputs,nullptr);require(output.uploads==before+1,"Larger Common size uploads only color");require(output.infos[0].textureDesc.width==20&&output.infos[0].textureDesc.height==30,"Common custom resolution");require(output.infos[2].textureDesc.width==2,"Larger Common size never upscales auxiliary");
auto nativeAux=plugin.snapshot()->planes[2].pixels;
auto metadata=[&](const std::string& key){OP_InfoDATSize size;plugin.getInfoDATSize(&size,nullptr);for(int row=1;row<size.rows;++row){String cells[4];OP_String* pointers[]={&cells[0],&cells[1],&cells[2],&cells[3]};OP_InfoDATEntries entries;entries.values=pointers;plugin.getInfoDATEntries(row,4,&entries,nullptr);if(cells[0].value==key)return cells[1].value;}return std::string{};};
inputs.resolution="eighth";before=output.uploads;plugin.execute(&output,&inputs,nullptr);
require(output.infos[0].textureDesc.width==1&&output.infos[0].textureDesc.height==2,"Eighth color size");
require(output.infos[2].textureDesc.width==1&&output.infos[2].textureDesc.height==2&&output.pixels[2].size()==8,"Auxiliary capped to main output with smaller allocation");
require(output.uploads==before+2,"Only resized color and active map uploaded");
require(plugin.snapshot()->reads==reads&&plugin.snapshot()->planes[2].pixels==nativeAux,"Resolution change reuses native snapshot");
require(metadata("outputs.disparity.width")=="1"&&metadata("outputs.disparity.height")=="2"&&metadata("outputs.disparity.native_width")=="2"&&metadata("outputs.disparity.native_height")=="4","Metadata separates capped output and native dimensions");
float first;std::memcpy(&first,output.pixels[2].data(),4);float expected=((*nativeAux)[0]+(*nativeAux)[1]+(*nativeAux)[2]+(*nativeAux)[3])*.25f;
require(std::fabs(first-expected)<.0001f&&first>1,"Downsampling preserves native disparity units");
inputs.resolution="custom";output.suggested.width=1;output.suggested.height=20;plugin.execute(&output,&inputs,nullptr);
require(output.infos[2].textureDesc.width==1&&output.infos[2].textureDesc.height==4,"Each dimension capped without upscaling the other");
inputs.resolution="quarter";plugin.execute(&output,&inputs,nullptr);require(output.infos[2].textureDesc.width==2&&output.infos[2].textureDesc.height==4,"Native size restored from cache when cap grows");
before=output.uploads;for(const auto* mode:{"half","useinput"}){inputs.resolution=mode;plugin.execute(&output,&inputs,nullptr);}
require(output.uploads==before+2,"Unchanged smaller auxiliary is not uploaded for larger caps");
for(const auto* mode:{"2x","4x","8x","fit","limit"}) {
    inputs.resolution=mode;plugin.execute(&output,&inputs,nullptr);
    unsigned expectedWidth=inputs.resolution=="2x"?16:inputs.resolution=="4x"?32:inputs.resolution=="8x"?64:2;
    require(output.infos[0].textureDesc.width==expectedWidth&&output.infos[0].textureDesc.height==expectedWidth*2,"Common scale and fit use file dimensions");
    require(output.infos[2].textureDesc.width==2&&output.infos[2].textureDesc.height==4,"All derived modes retain smaller native map");
}
inputs.boundWidth=inputs.boundHeight=32;inputs.resolution="limit";plugin.execute(&output,&inputs,nullptr);
require(output.infos[0].textureDesc.width==8&&output.infos[0].textureDesc.height==16,"Limit does not upscale color");
inputs.resolution="custom";
path=(root/"color.png").string();plugin.execute(&output,&inputs,nullptr);require(!plugin.availableMask(),"Source change clears output availability");for(auto b:output.pixels[2])require(b==0,"Old disparity cleared immediately");wait("ready");require(plugin.availableMask()==1,"Only color available");
path=(root/"broken.heic").string();wait("error");OP_InfoDATSize size;plugin.getInfoDATSize(&size,nullptr);require(size.rows==1&&size.cols==4,"Errors clear metadata");
path=(root/"color.png").string();wait("ready");context.fail=true;inputs.resolution="useinput";plugin.execute(&output,&inputs,nullptr);String err;plugin.getErrorString(&err,nullptr);require(!err.value.empty()&&!plugin.availableMask(),"Allocation failure reported and availability cleared");context.fail=false;wait("ready");
path=(root/"midtone.png").string();inputs.format="rgba32float";output.suggested.pixelFormat=OP_PixelFormat::RGBA32Float;wait("ready");
auto firstColor=[&](int c){float f;std::memcpy(&f,output.pixels[0].data()+c*4,4);return f;};
require(std::fabs(firstColor(0)-.2158605f)<.0001,"Managed output carries linear midtones");
node.workingColorSpace=OP_WorkingColorSpace::Passthrough;require(plugin.needsPublish(),"Working-space change needs a new upload");
plugin.execute(&output,&inputs,nullptr);
require(std::fabs(firstColor(0)-128.f/255)<.0001&&std::fabs(firstColor(1)-64.f/255)<.0001&&std::fabs(firstColor(2)-192.f/255)<.0001,"Passthrough output restores sRGB transfer without changing saturation");
require(output.infos[0].colorSpace==OP_ColorSpace::SRGB,"Unmanaged upload correctly labelled sRGB");
path.clear();wait("idle");require(!plugin.availableMask(),"Empty file clears buffers");
std::cout<<"PASS native/Common sizes, formats, multi-buffer uploads, cache, clearing, errors and recovery\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
