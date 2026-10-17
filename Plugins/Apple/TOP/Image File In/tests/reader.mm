#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <CoreImage/CoreImage.h>
#import <AVFoundation/AVFoundation.h>
#include "ImageData.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cstring>
using namespace ImageFile;
void require(bool v,const std::string& message){if(!v)throw std::runtime_error(message);}
std::string cell(const Result& r,const std::string& key){for(auto& row:r.rows)if(row[0]==key)return row[1];return {};}
CGImageRef fixture(bool alpha=false,CFStringRef space=kCGColorSpaceSRGB) {
    std::vector<uint8_t> pixels(16*8*4);
    for(size_t y=0;y<8;++y)for(size_t x=0;x<16;++x){auto* p=pixels.data()+(y*16+x)*4;
        p[0]=(x<8)==(y<4)?255:0;p[1]=x>=8?255:0;p[2]=y>=4?255:0;p[3]=alpha?128:255;
        if(alpha)for(int c=0;c<3;++c)p[c]=p[c]?128:0;
    }
    CFDataRef data=CFDataCreate(nullptr,pixels.data(),pixels.size());CGDataProviderRef provider=CGDataProviderCreateWithCFData(data);
    CGColorSpaceRef color=CGColorSpaceCreateWithName(space);
    CGImageRef image=CGImageCreate(16,8,8,32,64,color,kCGImageAlphaPremultipliedLast,provider,nullptr,false,kCGRenderingIntentDefault);
    CFRelease(color);CFRelease(provider);CFRelease(data);return image;
}
void writeImage(const std::filesystem::path& file,CFStringRef type,int orientation=1,int pages=1,bool alpha=false,int aux=0,CFStringRef space=kCGColorSpaceSRGB){
    NSURL* url=[NSURL fileURLWithPath:[NSString stringWithUTF8String:file.c_str()]];
    CGImageDestinationRef dest=CGImageDestinationCreateWithURL((__bridge CFURLRef)url,type,pages,nullptr);
    require(dest,"Cannot create fixture destination "+file.string());
    CGImageRef image=fixture(alpha,space);
    for(int i=0;i<pages;++i){
        NSDictionary* properties=@{(__bridge id)kCGImagePropertyOrientation:@(orientation),
            (__bridge id)kCGImagePropertyTIFFDictionary:@{(__bridge id)kCGImagePropertyTIFFArtist:@"Image File In generated fixture"},
            (__bridge id)kCGImagePropertyGIFDictionary:@{(__bridge id)kCGImagePropertyGIFDelayTime:@0.2}};
        CGImageDestinationAddImage(dest,image,(__bridge CFDictionaryRef)properties);
        if(aux&&i==0){
            float disparity[]={.5f,1,2,4,8,16,32,64};
            NSDictionary* desc=@{@"Width":@4,@"Height":@2,@"BytesPerRow":@16,@"PixelFormat":@(aux==2?kCVPixelFormatType_DepthFloat32:kCVPixelFormatType_DisparityFloat32)};
            NSDictionary* data=@{(__bridge id)kCGImageAuxiliaryDataInfoData:[NSData dataWithBytes:disparity length:sizeof(disparity)],(__bridge id)kCGImageAuxiliaryDataInfoDataDescription:desc};
            CGImageDestinationAddAuxiliaryDataInfo(dest,aux==2?kCGImageAuxiliaryDataTypeDepth:kCGImageAuxiliaryDataTypeDisparity,(__bridge CFDictionaryRef)data);
            std::vector<uint8_t> mask={0,32,64,128,160,192,224,255};
            const unsigned mw=aux==4?8:4,mh=aux==4?4:2;
            if(aux==4){mask.resize(mw*mh);for(size_t j=0;j<mask.size();++j)mask[j]=uint8_t(j*255/(mask.size()-1));}
            NSDictionary* matte=@{(__bridge id)kCGImageAuxiliaryDataInfoData:[NSData dataWithBytes:mask.data() length:mask.size()],
                (__bridge id)kCGImageAuxiliaryDataInfoDataDescription:@{@"Width":@(mw),@"Height":@(mh),@"BytesPerRow":@(mw),@"PixelFormat":@(kCVPixelFormatType_OneComponent8)}};
            CGImageDestinationAddAuxiliaryDataInfo(dest,kCGImageAuxiliaryDataTypePortraitEffectsMatte,(__bridge CFDictionaryRef)matte);
            if(aux==3)CGImageDestinationAddAuxiliaryDataInfo(dest,kCGImageAuxiliaryDataTypeHDRGainMap,(__bridge CFDictionaryRef)matte);
            for(CFStringRef kind:{kCGImageAuxiliaryDataTypeSemanticSegmentationSkinMatte,kCGImageAuxiliaryDataTypeSemanticSegmentationHairMatte,
                kCGImageAuxiliaryDataTypeSemanticSegmentationTeethMatte,kCGImageAuxiliaryDataTypeSemanticSegmentationGlassesMatte,kCGImageAuxiliaryDataTypeSemanticSegmentationSkyMatte})
                CGImageDestinationAddAuxiliaryDataInfo(dest,kind,(__bridge CFDictionaryRef)matte);
        }
    }
    bool ok=CGImageDestinationFinalize(dest);CFRelease(image);CFRelease(dest);require(ok,"Cannot encode fixture "+file.string());
}
std::shared_ptr<Result> load(const std::filesystem::path& path,std::string image="primary",std::string decode="auto",uint32_t mask=AllAuxiliaries) {
    Request r;r.path=path.string();r.image=image;r.decode=decode;r.auxiliaryMask=mask;return decodeFile(r,[]{return false;});
}
void handcrafted(const std::filesystem::path& root){
    // Minimal uncompressed 2x2 TGA and flattened RGB PSD, authored in this test.
    std::vector<uint8_t> tga(18,0);tga[2]=2;tga[12]=2;tga[14]=2;tga[16]=24;tga[17]=32;
    tga.insert(tga.end(),{0,0,255,0,255,0,255,0,0,255,255,255});
    std::ofstream(root/"tiny.tga",std::ios::binary).write((char*)tga.data(),tga.size());
    std::vector<uint8_t> psd={'8','B','P','S',0,1,0,0,0,0,0,0,0,3,0,0,0,2,0,0,0,2,0,8,0,3};
    psd.insert(psd.end(),14,0);psd.insert(psd.end(),{255,0,0,255,0,255,0,255,0,0,255,255});
    std::ofstream(root/"tiny.psd",std::ios::binary).write((char*)psd.data(),psd.size());
}
void colorPatch(const std::filesystem::path& file) {
    uint8_t pixel[]={128,64,192,255};
    auto data=CFDataCreate(nullptr,pixel,4);auto provider=CGDataProviderCreateWithCFData(data);
    auto space=CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    auto image=CGImageCreate(1,1,8,32,4,space,kCGImageAlphaPremultipliedLast,provider,nullptr,false,kCGRenderingIntentDefault);
    NSURL* url=[NSURL fileURLWithPath:[NSString stringWithUTF8String:file.c_str()]];
    auto destination=CGImageDestinationCreateWithURL((__bridge CFURLRef)url,CFSTR("public.png"),1,nullptr);
    CGImageDestinationAddImage(destination,image,nullptr);require(CGImageDestinationFinalize(destination),"Write midtone reference");
    CFRelease(destination);CFRelease(image);CFRelease(space);CFRelease(provider);CFRelease(data);
}
int main(int argc,char** argv){@autoreleasepool{try{
    require(argc==2,"Supply fixture directory");std::filesystem::path root=argv[1];std::filesystem::create_directories(root);
    const std::pair<const char*,CFStringRef> formats[]={{"color.png",CFSTR("public.png")},{"color.jpg",CFSTR("public.jpeg")},{"color.tiff",CFSTR("public.tiff")},{"color.gif",CFSTR("com.compuserve.gif")},{"color.heic",CFSTR("public.heic")}};
    for(auto f:formats){writeImage(root/f.first,f.second);auto r=load(root/f.first);require(r->planes[0].width==16&&r->planes[0].height==8,"Decoded dimensions "+std::string(f.first));require(r->rows.size()>15,"Metadata rows");for(size_t i=1;i<BufferCount;++i)require(!r->planes[i].present(),"No invented auxiliary outputs");std::cout<<"decoded "<<f.first<<'\n';}
    handcrafted(root);
    colorPatch(root/"midtone.png");auto midtone=load(root/"midtone.png");
    const float expectedLinear[]={.2158605f,.05126946f,.5271151f};
    for(int c=0;c<3;++c)require(std::fabs((*midtone->planes[0].pixels)[c]-expectedLinear[c])<.0003,"sRGB profile to linear-light midtones channel "+std::to_string(c)+" actual "+std::to_string((*midtone->planes[0].pixels)[c]));
    for(auto name:{"tiny.tga","tiny.psd"}){auto r=load(root/name);require(r->planes[0].width==2&&r->planes[0].height==2,name);}
    // Known independent top-left source layout: red, green / blue, white.
    const int topLeftQuadrant[]={0,1,3,2,0,2,3,1};
    for(int orientation=1;orientation<=8;++orientation){
        auto file=root/("orientation"+std::to_string(orientation)+".tiff");writeImage(file,CFSTR("public.tiff"),orientation);
        auto r=load(file);const auto& p=r->planes[0];require(p.width==(orientation>=5?8:16)&&p.height==(orientation>=5?16:8),"Orientation dimensions");
        const float* pixel=p.pixels->data()+size_t(p.height-1)*p.width*4;
        float expected[4][3]={{1,0,0},{0,1,0},{0,0,1},{1,1,1}};
        for(int c=0;c<3;++c)require(std::fabs(pixel[c]-expected[topLeftQuadrant[orientation-1]][c])<.015,"Orientation corner "+std::to_string(orientation)+" component "+std::to_string(c)+" actual "+std::to_string(pixel[c]));
    }
    writeImage(root/"alpha.png",CFSTR("public.png"),1,1,true);auto alpha=load(root/"alpha.png");
    require(std::fabs((*alpha->planes[0].pixels)[3]-128.f/255)<.002,"Alpha preserved");
    writeImage(root/"p3.png",CFSTR("public.png"),1,1,false,false,kCGColorSpaceDisplayP3);auto p3=load(root/"p3.png");
    bool outside=false;for(float v:*p3->planes[0].pixels)if(v>1.01||v<-.01)outside=true;require(outside,"Wide gamut conversion preserves out-of-sRGB values");
    writeImage(root/"pages.tiff",CFSTR("public.tiff"),1,3);auto pages=load(root/"pages.tiff","2");require(pages->images.size()==3&&pages->selected==2,"Multi-page selection");
    auto fallback=load(root/"pages.tiff","99");require(fallback->selectionFallback&&fallback->selected==fallback->primary&&!fallback->warnings.empty(),"Missing image fallback");
    writeImage(root/"animated.gif",CFSTR("com.compuserve.gif"),1,2);require(load(root/"animated.gif","1")->selected==1,"GIF image selection");
    writeImage(root/"collection.heic",CFSTR("public.heic"),1,2);require(load(root/"collection.heic","1")->images.size()==2,"HEIC image collection");
    writeImage(root/"portrait.heic",CFSTR("public.heic"),6,1,false,true);auto portrait=load(root/"portrait.heic");
    for(auto& w:portrait->warnings)std::cerr<<w<<'\n';
    require(portrait->planes[2].present(),"Generated HEIC disparity present");
    require(portrait->planes[2].width==2&&portrait->planes[2].height==4,"Disparity oriented native dimensions");
    float max=0;for(float v:*portrait->planes[2].pixels)max=std::max(max,v);require(max>60,"Disparity keeps native values above one");
    require(!portrait->planes[1].present(),"Disparity does not invent a depth layer");
    require(portrait->planes[3].present(),"Portrait matte decoded");
    writeImage(root/"portrait-large-mattes.heic",CFSTR("public.heic"),6,1,false,4);
    auto mixed=load(root/"portrait-large-mattes.heic");
    require(mixed->planes[2].width==2&&mixed->planes[2].height==4&&mixed->planes[3].width==4&&mixed->planes[3].height==8,"Mixed native auxiliary sizes for Common resolution checks");
    for(size_t slot=4;slot<=8;++slot)require(portrait->planes[slot].present(),std::string("Semantic matte decoded: ")+Kinds[slot]);
    auto upright=load(root/"portrait.heic");
    require(std::fabs((*upright->planes[2].pixels)[0]-64)<2,"Oriented disparity bottom-left corner");
    writeImage(root/"depth.heic",CFSTR("public.heic"),1,1,false,2);
    auto depth=load(root/"depth.heic");require(depth->planes[1].present()&&!depth->planes[2].present(),"Native depth stays separate from disparity");
    require(std::fabs((*depth->planes[1].pixels)[0]-8)<.5,"Native depth values preserved: "+std::to_string((*depth->planes[1].pixels)[0]));
    const float bottomLeftDisparity[]={8,64,4,.5f,4,64,8,.5f};
    for(int orientation=1;orientation<=8;++orientation) {
        auto file=root/("aux-orientation"+std::to_string(orientation)+".heic");
        writeImage(file,CFSTR("public.heic"),orientation,1,false,1);auto r=load(file);
        require(std::fabs((*r->planes[2].pixels)[0]-bottomLeftDisparity[orientation-1])<.5,"All EXIF orientations align auxiliary pixels");
    }
    writeImage(root/"selected-aux.heic",CFSTR("public.heic"),1,2,false,1);
    require(load(root/"selected-aux.heic","0")->planes[2].present(),"Selected first image owns its auxiliary");
    auto second=load(root/"selected-aux.heic","1");
    for(size_t slot=1;slot<BufferCount;++slot)require(!second->planes[slot].present(),"Second image cannot inherit primary auxiliaries");
    writeImage(root/"gain-apple.heic",CFSTR("public.heic"),1,1,false,3);
    require(load(root/"gain-apple.heic")->planes[9].present(),"Apple HDR gain map decoded separately");
    // An HDR float TIFF exercises the entire path without requiring a camera.
    std::vector<float> hdr(4*4*4,1);for(size_t i=0;i<hdr.size();i+=4){hdr[i]=4;hdr[i+1]=2;hdr[i+2]=.123456f;}
    CGColorSpaceRef linear=CGColorSpaceCreateWithName(kCGColorSpaceExtendedLinearSRGB);
    CIImage* ci=[CIImage imageWithBitmapData:[NSData dataWithBytes:hdr.data() length:hdr.size()*4] bytesPerRow:4*4*4 size:CGSizeMake(4,4) format:kCIFormatRGBAf colorSpace:linear];
    CIContext* ctx=[CIContext contextWithOptions:@{}];NSError* error=nil;
    NSURL* url=[NSURL fileURLWithPath:[NSString stringWithUTF8String:(root/"hdr.tiff").c_str()]];
    require([ctx writeTIFFRepresentationOfImage:ci toURL:url format:kCIFormatRGBAf colorSpace:linear options:@{} error:&error],"Write HDR TIFF");
    if(@available(macOS 15.0,*)) {
        std::vector<float> sdr=hdr;for(size_t i=0;i<sdr.size();++i)if(i%4!=3)sdr[i]/=4;
        CIImage* base=[CIImage imageWithBitmapData:[NSData dataWithBytes:sdr.data() length:sdr.size()*4] bytesPerRow:64 size:CGSizeMake(4,4) format:kCIFormatRGBAf colorSpace:linear];
        CGColorSpaceRef srgb=CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        for(bool rgb:{false,true}) {
            auto file=root/(rgb?"gain-rgb.heic":"gain-mono.heic");
            NSURL* gainURL=[NSURL fileURLWithPath:[NSString stringWithUTF8String:file.c_str()]];
            require([ctx writeHEIFRepresentationOfImage:base toURL:gainURL format:kCIFormatRGBA8 colorSpace:srgb
                options:@{kCIImageRepresentationHDRImage:ci,kCIImageRepresentationHDRGainMapAsRGB:@(rgb)} error:&error],"Write HDR gain map HEIC");
            auto gain=load(file);for(auto& warning:gain->warnings)std::cerr<<warning<<'\n';
            require(gain->planes[9].present()||gain->planes[10].present(),"Generated HDR gain map decoded");
            require(gain->planes[10].channels==(rgb?3u:1u),"Native ISO gain-map channel count");
            require(cell(*gain,"imageio.images[0].auxiliary.iso_gain.kCGImageAuxiliaryDataInfoMetadata.HDRToneMap:Version")=="1","Gain-map metadata values");
            for(size_t slot=9;slot<=10;++slot)if(gain->planes[slot].present())std::cout<<Kinds[slot]<<" channels="<<gain->planes[slot].channels<<'\n';
        }
        CFRelease(srgb);
    }
    CFRelease(linear);
    // Retained native payloads must survive source/CGImageSource destruction and
    // reproduce the eager path exactly, including every orientation and gain kind.
    std::vector<std::string> deferredFiles={"portrait.heic","depth.heic","gain-apple.heic"};
    for(int orientation=1;orientation<=8;++orientation)deferredFiles.push_back("aux-orientation"+std::to_string(orientation)+".heic");
    if(@available(macOS 15.0,*)){deferredFiles.push_back("gain-mono.heic");deferredFiles.push_back("gain-rgb.heic");}
    for(const auto& name:deferredFiles) {
        auto eager=load(root/name),idle=load(root/name,"primary","auto",0);
        require(idle->preparations==0,"Inactive decode has zero float preparations");
        for(size_t slot=1;slot<BufferCount;++slot) {
            require(idle->planes[slot].available()==eager->planes[slot].available(),"Deferred availability matches decoded payload");
            if(!idle->planes[slot].available())continue;
            require(!idle->planes[slot].present()&&bool(idle->prepare[slot]),"Idle map retains validated native payload only");
            auto ready=idle->prepare[slot]([]{return false;});const auto& expected=eager->planes[slot];
            require(ready.width==expected.width&&ready.height==expected.height&&ready.channels==expected.channels,"Deferred orientation/native dimensions");
            require(*ready.pixels==*expected.pixels,"Deferred samples exactly match eager decoding");
        }
    }
    auto decodedHDR=load(root/"hdr.tiff","primary","hdr");require((*decodedHDR->planes[0].pixels)[0]>3.9,"HDR above one preserved");
    require(std::fabs((*decodedHDR->planes[0].pixels)[2]-.123456f)<.00001,"Float precision preserved");
    std::ofstream(root/"broken.heic")<<"not an image";bool failed=false;try{load(root/"broken.heic");}catch(...){failed=true;}require(failed,"Malformed image rejected");
    Request cancelled;cancelled.path=(root/"color.png").string();failed=false;try{decodeFile(cancelled,[]{return true;});}catch(...){failed=true;}require(failed,"Cancellation observed");
    Result limited;Table table(limited);table.add("duplicate","one","g","s");table.add("duplicate","two","g","s");table.add("large",std::string(70000,'x'),"g","s");for(int i=0;i<21000;++i)table.add("key"+std::to_string(i),"value","g","s");table.finish();require(limited.rows.size()==20000&&!limited.warnings.empty(),"Metadata limits");require(cell(limited,"duplicate#2")=="two","Unique duplicate keys");
    std::cout<<"PASS formats, orientation, alpha, ICC, HDR, collections, disparity, matte, malformed data and metadata limits\n";
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}return 0;}}
