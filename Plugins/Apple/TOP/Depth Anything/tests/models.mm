#include <Python.h>
#include "HostStubs.h"
#include <vector>
#include <mutex>
#include <atomic>
#include <string>
#import <Vision/Vision.h>
#import <CoreML/CoreML.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#define private public
#include "AppleDepth.h"
#undef private
#include <cmath>
#include <iostream>
#include <stdexcept>

static void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
class Inputs : public TestHost::Inputs {
public:
    std::string choice="Smallf16", path;
    mutable bool enabled=false;
    const char* getParString(const char*) const override { return choice.c_str(); }
    const char* getParFilePath(const char*) const override { return path.c_str(); }
    void enablePar(const char* name,bool on) const override { if(std::string(name)=="Modelfile") enabled=on; }
};
static CVPixelBufferRef frame() {
    CVPixelBufferRef buffer=nullptr;
    require(CVPixelBufferCreate(nullptr,320,240,kCVPixelFormatType_32BGRA,nullptr,&buffer)==kCVReturnSuccess,"pixel buffer");
    CVPixelBufferLockBaseAddress(buffer,0);
    auto* pixels=static_cast<unsigned char*>(CVPixelBufferGetBaseAddress(buffer));
    const size_t stride=CVPixelBufferGetBytesPerRow(buffer);
    for(size_t y=0;y<240;++y) for(size_t x=0;x<320;++x) {
        auto* p=pixels+y*stride+x*4;
        p[0]=static_cast<unsigned char>(x*255/319);
        p[1]=static_cast<unsigned char>(y*255/239);
        p[2]=((x/40+y/30)%2)*200; p[3]=255;
    }
    CVPixelBufferUnlockBaseAddress(buffer,0);
    return buffer;
}
int main(int argc,char** argv) {
    try { @autoreleasepool {
        require(argc==2,"Pass the built plugin bundle");
        const std::string bundle=argv[1], binary=bundle+"/Contents/MacOS/AppleDepthAnything";
        TD::OP_NodeInfo node{}; node.opId=1; node.pluginPath=binary.c_str();
        AppleDepth plugin(&node,nullptr);
        Inputs inputs;
        TD::TOP_GeneralInfo general;
        for(const char* model:{"Smallf16","Smallf16p6"}) {
            inputs.choice=model;
            plugin.getGeneralInfo(&general,&inputs,nullptr);
            require(!inputs.enabled,"file parameter disabled for bundled choices");
            auto path=plugin.resolveModel(&inputs);
            require(path.find(bundle)==0 && path.find(".mlmodelc")!=std::string::npos,"bundle-relative compiled path");
            plugin.modelError.clear();
            plugin.runDepth(frame(),path.c_str(),true,false);
            if(!plugin.hasCachedDepth.load()) throw std::runtime_error(plugin.modelError);
            require(plugin.cachedDepthW>1 && plugin.cachedDepthH>1,"depth dimensions");
            float low=1,high=0;
            for(float value:plugin.cachedDepthData) {
                require(std::isfinite(value) && value>=0 && value<=1,"normalized finite depth");
                low=std::min(low,value); high=std::max(high,value);
            }
            require(high-low>.5f,"nonconstant depth for synthetic image");
            auto* loaded=plugin.depthModel;
            require(plugin.ensureModelLoaded(path.c_str()) && loaded==plugin.depthModel,"loaded model reuse");
            std::cout << model << ": Vision depth " << plugin.cachedDepthW << "x" << plugin.cachedDepthH << ", finite range " << low << ".." << high << '\n';
        }
        inputs.choice="Custom"; inputs.path=bundle+"/Contents/Resources/Models/DepthAnythingV2SmallF16.mlmodelc";
        plugin.getGeneralInfo(&general,&inputs,nullptr);
        require(inputs.enabled && plugin.resolveModel(&inputs)==inputs.path,"custom path enabled and preserved");
        plugin.runDepth(frame(),"/nonexistent/depth-test-model.mlpackage",true,false);
        require(!plugin.hasCachedDepth && !plugin.modelError.empty(),"invalid selection clears cached output and reports error");
        std::cout << "Model resolution, switching, caching, and failure checks passed\n";
    }} catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
