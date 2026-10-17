#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <CoreImage/CoreImage.h>
#import <AVFoundation/AVFoundation.h>
#include "ImageData.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>

namespace ImageFile {
namespace {
template<class T> struct Owned {
    T p=nullptr;
    explicit Owned(T value):p(value) {}
    ~Owned() { if(p) CFRelease(p); }
    Owned(const Owned&)=delete;
    operator T() const { return p; }
};
std::string str(CFStringRef s) {
    if(!s) return {};
    const auto length=CFStringGetLength(s);
    if(length>1048576) return "[string exceeds metadata input limit]";
    std::string out(CFStringGetMaximumSizeForEncoding(length,kCFStringEncodingUTF8)+1,'\0');
    if(!CFStringGetCString(s,out.data(),out.size(),kCFStringEncodingUTF8)) return "[invalid string]";
    out.resize(std::strlen(out.c_str())); return out;
}
std::string number(double n) { std::ostringstream s; s<<std::setprecision(17)<<n; return s.str(); }
void checkCancel(const Cancel& cancelled) { if(cancelled()) throw std::runtime_error("Image read superseded"); }
int64_t integer(CFDictionaryRef dict,CFStringRef key,int64_t fallback=0) {
    if(!dict) return fallback;
    auto p=CFDictionaryGetValue(dict,key);
    int64_t value=0;
    return p && CFGetTypeID(p)==CFNumberGetTypeID() && CFNumberGetValue((CFNumberRef)p,kCFNumberSInt64Type,&value)?value:fallback;
}
void flatten(CFTypeRef value,const std::string& key,const std::string& group,Table& table,int depth=0) {
    if(!value||table.full()) return;
    if(depth>32) { table.warning("Nested metadata truncated at depth 32"); return; }
    auto type=CFGetTypeID(value);
    if(type==CFDictionaryGetTypeID()) {
        auto d=(CFDictionaryRef)value; CFIndex count=CFDictionaryGetCount(d);
        if(count>20000) { table.warning("Metadata dictionary exceeds 20000 entries"); return; }
        std::vector<const void*> keys(count),values(count);
        CFDictionaryGetKeysAndValues(d,keys.data(),values.data());
        std::vector<std::pair<std::string,CFTypeRef>> ordered;
        for(CFIndex i=0;i<count;++i) if(CFGetTypeID(keys[i])==CFStringGetTypeID()) ordered.emplace_back(str((CFStringRef)keys[i]),values[i]);
        std::sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b){return a.first<b.first;});
        for(auto& entry:ordered) flatten(entry.second,key+"."+escaped(entry.first),group,table,depth+1);
        if(!count) table.add(key,"{}",group,"ImageIO");
    } else if(type==CFArrayGetTypeID()) {
        auto a=(CFArrayRef)value; auto count=CFArrayGetCount(a);
        for(CFIndex i=0;i<std::min<CFIndex>(count,20000)&&!table.full();++i)
            flatten(CFArrayGetValueAtIndex(a,i),key+"["+std::to_string(i)+"]",group,table,depth+1);
        if(count>20000) table.warning("Metadata array truncated at 20000 entries");
        if(!count) table.add(key,"[]",group,"ImageIO");
    } else if(type==CFStringGetTypeID()) table.add(key,str((CFStringRef)value),group,"ImageIO");
    else if(type==CFBooleanGetTypeID()) table.add(key,CFBooleanGetValue((CFBooleanRef)value)?"true":"false",group,"ImageIO");
    else if(type==CFNumberGetTypeID()) {
        auto n=(CFNumberRef)value;
        if(CFNumberIsFloatType(n)) { double v=0; CFNumberGetValue(n,kCFNumberDoubleType,&v); table.add(key,number(v),group,"ImageIO"); }
        else { int64_t v=0; CFNumberGetValue(n,kCFNumberSInt64Type,&v); table.add(key,std::to_string(v),group,"ImageIO"); }
    } else if(type==CFDataGetTypeID()) table.add(key+".size_bytes",std::to_string(CFDataGetLength((CFDataRef)value)),group,"ImageIO");
    else if(type==CFDateGetTypeID()) table.add(key+".unix_seconds",number(CFDateGetAbsoluteTime((CFDateRef)value)+kCFAbsoluteTimeIntervalSince1970),group,"ImageIO");
    else if(type==CGImageMetadataTagGetTypeID()) {
        Owned<CFTypeRef> contents(CGImageMetadataTagCopyValue((CGImageMetadataTagRef)value));
        flatten(contents,key,group,table,depth+1);
    } else if(type==CGImageMetadataGetTypeID()) {
        // The CF representation of each top-level tag already contains arrays/structures.
        Table* target=&table;
        CGImageMetadataEnumerateTagsUsingBlock((CGImageMetadataRef)value,nullptr,nullptr,^bool(CFStringRef path,CGImageMetadataTagRef tag) {
            Owned<CFTypeRef> v(CGImageMetadataTagCopyValue(tag));
            flatten(v,key+"."+escaped(str(path)),group,*target,depth+1);
            return !target->full();
        });
    } else if(type==CGColorSpaceGetTypeID()) {
        Owned<CFStringRef> name(CGColorSpaceCopyName((CGColorSpaceRef)value));
        table.add(key,name.p?str(name):"unnamed",group,"ImageIO");
    } else table.add(key,"[non-text metadata]",group,"ImageIO");
}
// Read supported non-planar auxiliary rasters without applying a color transform.
Plane raster(const uint8_t* bytes,size_t length,uint64_t width,uint64_t height,uint64_t stride,OSType format,bool materialize=true) {
    uint32_t channels=1,component=1; enum Encoding { U8,U16,BE16,F16,F32 } encoding=U8;
    bool bgra=false;
    switch(format) {
        case kCVPixelFormatType_OneComponent8:break;
        case kCVPixelFormatType_OneComponent16:component=2;encoding=U16;break;
        case kCVPixelFormatType_OneComponent16Half:case kCVPixelFormatType_DepthFloat16:case kCVPixelFormatType_DisparityFloat16:component=2;encoding=F16;break;
        case kCVPixelFormatType_OneComponent32Float:case kCVPixelFormatType_DepthFloat32:case kCVPixelFormatType_DisparityFloat32:component=4;encoding=F32;break;
        case kCVPixelFormatType_TwoComponent8:channels=2;break;
        case kCVPixelFormatType_TwoComponent16:channels=2;component=2;encoding=U16;break;
        case kCVPixelFormatType_TwoComponent16Half:channels=2;component=2;encoding=F16;break;
        case kCVPixelFormatType_TwoComponent32Float:channels=2;component=4;encoding=F32;break;
        case kCVPixelFormatType_24RGB:channels=3;break;
        case kCVPixelFormatType_48RGB:channels=3;component=2;encoding=BE16;break;
        case kCVPixelFormatType_32BGRA:bgra=true;[[fallthrough]];
        case kCVPixelFormatType_32RGBA:channels=4;break;
        case kCVPixelFormatType_64RGBALE:channels=4;component=2;encoding=U16;break;
        case kCVPixelFormatType_64RGBAHalf:channels=4;component=2;encoding=F16;break;
        case kCVPixelFormatType_128RGBAFloat:channels=4;component=4;encoding=F32;break;
        default:throw std::runtime_error("Unsupported auxiliary pixel format "+std::to_string(format));
    }
    if(width>UINT32_MAX||height>UINT32_MAX) throw std::runtime_error("Auxiliary dimensions overflow");
    Plane p; p.width=uint32_t(width);p.height=uint32_t(height);p.channels=channels;
    size_t count=imageElements(p.width,p.height,channels);
    uint64_t row=width*channels*component;
    if(!bytes || length>MaxImageBytes || stride<row || stride>MaxImageBytes || (height-1)>SIZE_MAX/stride || (height-1)*stride>length || row>length-(height-1)*stride)
        throw std::runtime_error("Truncated auxiliary raster or invalid row stride");
    if(!materialize)return p;
    auto pixels=std::make_shared<std::vector<float>>(count);
    p.pixels=pixels;
    for(size_t y=0;y<height;++y) for(size_t x=0;x<width;++x) for(size_t c=0;c<channels;++c) {
        size_t sourceChannel=bgra&&c<3?2-c:c;
        const uint8_t* data=bytes+y*stride+(x*channels+sourceChannel)*component;
        float value=0;
        if(encoding==U8) value=*data/255.f;
        else if(encoding==U16) { uint16_t v;std::memcpy(&v,data,2);value=v/65535.f; }
        else if(encoding==BE16) value=(uint16_t(data[0])*256+data[1])/65535.f;
        else if(encoding==F16) { _Float16 v;std::memcpy(&v,data,2);value=float(v); }
        else std::memcpy(&value,data,4);
        (*pixels)[(y*width+x)*channels+c]=value;
    }
    return p;
}
Plane pixelBuffer(CVPixelBufferRef buffer,bool materialize=true) {
    if(!buffer||CVPixelBufferIsPlanar(buffer)) throw std::runtime_error("Missing or unsupported planar auxiliary buffer");
    if(CVPixelBufferLockBaseAddress(buffer,kCVPixelBufferLock_ReadOnly)!=kCVReturnSuccess) throw std::runtime_error("Could not lock auxiliary buffer");
    struct Unlock { CVPixelBufferRef b; ~Unlock(){CVPixelBufferUnlockBaseAddress(b,kCVPixelBufferLock_ReadOnly);} } unlock{buffer};
    return raster((const uint8_t*)CVPixelBufferGetBaseAddress(buffer),CVPixelBufferGetDataSize(buffer),
        CVPixelBufferGetWidth(buffer),CVPixelBufferGetHeight(buffer),CVPixelBufferGetBytesPerRow(buffer),CVPixelBufferGetPixelFormatType(buffer),materialize);
}
Plane rawAuxiliary(CFDictionaryRef dict,bool materialize=true) {
    auto data=(CFDataRef)CFDictionaryGetValue(dict,kCGImageAuxiliaryDataInfoData);
    auto description=(CFDictionaryRef)CFDictionaryGetValue(dict,kCGImageAuxiliaryDataInfoDataDescription);
    if(!data||CFGetTypeID(data)!=CFDataGetTypeID()||!description||CFGetTypeID(description)!=CFDictionaryGetTypeID()) throw std::runtime_error("Missing auxiliary data/description");
    return raster(CFDataGetBytePtr(data),CFDataGetLength(data),integer(description,CFSTR("Width")),integer(description,CFSTR("Height")),
        integer(description,CFSTR("BytesPerRow")),OSType(integer(description,CFSTR("PixelFormat"))),materialize);
}
Plane planarGainMap(CGImageSourceRef source,size_t index,CFDictionaryRef dictionary) {
    // Image I/O may return a planar RGB gain descriptor without CFData.
    // Unpack this selected image without applying a display-color transform.
    auto description=(CFDictionaryRef)CFDictionaryGetValue(dictionary,kCGImageAuxiliaryDataInfoDataDescription);
    if(!description||CFGetTypeID(description)!=CFDictionaryGetTypeID()) throw std::runtime_error("Missing gain-map description");
    auto format=OSType(integer(description,CFSTR("PixelFormat")));
    if(format!=kCVPixelFormatType_420YpCbCr8BiPlanarFullRange && format!=kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange)
        throw std::runtime_error("Unsupported planar gain-map format");
    auto width=integer(description,CFSTR("Width")),height=integer(description,CFSTR("Height"));
    if(width<=0||height<=0||width>65536||height>65536) throw std::runtime_error("Invalid gain-map dimensions");
    imageElements(uint32_t(width),uint32_t(height),4);
    CIImage* image=[CIImage imageWithCGImageSource:source index:index options:@{
        kCIImageAuxiliaryHDRGainMap:@YES,kCIImageColorSpace:[NSNull null],kCIImageApplyOrientationProperty:@NO,kCIImageCacheImmediately:@YES}];
    if(!image||image.extent.size.width!=width||image.extent.size.height!=height)
        throw std::runtime_error("Could not decode the native gain-map raster");
    CIContext* context=[CIContext contextWithOptions:@{kCIContextWorkingColorSpace:[NSNull null],kCIContextOutputColorSpace:[NSNull null],kCIContextCacheIntermediates:@NO}];
    if(!context) throw std::runtime_error("Could not initialize gain-map unpacking");
    std::vector<float> rgba(imageElements(uint32_t(width),uint32_t(height),4));
    [context render:image toBitmap:rgba.data() rowBytes:size_t(width)*16 bounds:image.extent format:kCIFormatRGBAf colorSpace:nullptr];
    Plane result;result.width=uint32_t(width);result.height=uint32_t(height);result.channels=3;
    auto pixels=std::make_shared<std::vector<float>>(imageElements(result.width,result.height,3));
    for(size_t i=0;i<pixels->size()/3;++i)std::copy_n(rgba.data()+i*4,3,pixels->data()+i*3);
    result.pixels=pixels;
    return result;
}
void stereoRoles(CFDictionaryRef properties,size_t index,bool& left,bool& right,bool& mono) {
    if(!properties) return;
    auto inspect=[&](CFDictionaryRef d) {
        left|=CFDictionaryGetValue(d,kCGImagePropertyGroupImageIsLeftImage)==kCFBooleanTrue || integer(d,kCGImagePropertyGroupImageIndexLeft,-1)==int64_t(index);
        right|=CFDictionaryGetValue(d,kCGImagePropertyGroupImageIsRightImage)==kCFBooleanTrue || integer(d,kCGImagePropertyGroupImageIndexRight,-1)==int64_t(index);
        if(@available(macOS 15.0,*)) mono|=CFDictionaryGetValue(d,kCGImagePropertyGroupImageIsMonoscopicImage)==kCFBooleanTrue || integer(d,kCGImagePropertyGroupImageIndexMonoscopic,-1)==int64_t(index);
    };
    inspect(properties);
    auto groups=(CFArrayRef)CFDictionaryGetValue(properties,kCGImagePropertyGroups);
    if(groups&&CFGetTypeID(groups)==CFArrayGetTypeID())for(CFIndex i=0;i<std::min<CFIndex>(CFArrayGetCount(groups),10000);++i) {
        auto value=CFArrayGetValueAtIndex(groups,i);
        if(CFGetTypeID(value)==CFDictionaryGetTypeID())inspect((CFDictionaryRef)value);
    }
}
void calibration(AVDepthData* depth,const std::string& key,Table& table) {
    auto add=[&](const std::string& k,const std::string& v){table.add(key+"."+k,v,"Depth","AVFoundation");};
    add("accuracy",depth.depthDataAccuracy==AVDepthDataAccuracyAbsolute?"absolute":"relative");
    add("quality",depth.depthDataQuality==AVDepthDataQualityHigh?"high":"low");
    add("filtered",depth.depthDataFiltered?"true":"false");
    auto camera=depth.cameraCalibrationData;
    if(!camera) return;
    auto matrix=camera.intrinsicMatrix;
    for(int row=0;row<3;++row) for(int col=0;col<3;++col)
        add("calibration.intrinsic["+std::to_string(row)+"]["+std::to_string(col)+"]",number(matrix.columns[col][row]));
    auto extrinsic=camera.extrinsicMatrix;
    for(int row=0;row<3;++row) for(int col=0;col<4;++col)
        add("calibration.extrinsic["+std::to_string(row)+"]["+std::to_string(col)+"]",number(extrinsic.columns[col][row]));
    add("calibration.reference_width",number(camera.intrinsicMatrixReferenceDimensions.width));
    add("calibration.reference_height",number(camera.intrinsicMatrixReferenceDimensions.height));
    add("calibration.pixel_size_mm",number(camera.pixelSize));
    add("calibration.distortion_center_x",number(camera.lensDistortionCenter.x));
    add("calibration.distortion_center_y",number(camera.lensDistortionCenter.y));
    for(auto item: {std::make_pair("lens_distortion",camera.lensDistortionLookupTable),std::make_pair("inverse_lens_distortion",camera.inverseLensDistortionLookupTable)}) {
        NSData* values=item.second;
        for(size_t i=0;i<values.length/sizeof(float)&&i<20000;++i) { float v; std::memcpy(&v,(const uint8_t*)values.bytes+i*sizeof(float),sizeof(float));add(std::string("calibration.")+item.first+"["+std::to_string(i)+"]",number(v)); }
    }
}
CFStringRef auxiliaryType(size_t slot) {
    switch(slot) {
        case 1:return kCGImageAuxiliaryDataTypeDepth; case 2:return kCGImageAuxiliaryDataTypeDisparity;
        case 3:return kCGImageAuxiliaryDataTypePortraitEffectsMatte;
        case 4:return kCGImageAuxiliaryDataTypeSemanticSegmentationSkinMatte;
        case 5:return kCGImageAuxiliaryDataTypeSemanticSegmentationHairMatte;
        case 6:return kCGImageAuxiliaryDataTypeSemanticSegmentationTeethMatte;
        case 7:return kCGImageAuxiliaryDataTypeSemanticSegmentationGlassesMatte;
        case 8:return kCGImageAuxiliaryDataTypeSemanticSegmentationSkyMatte;
        case 9:return kCGImageAuxiliaryDataTypeHDRGainMap;
        case 10:if(@available(macOS 15.0,*)) return kCGImageAuxiliaryDataTypeISOGainMap;return nullptr;
    } return nullptr;
}
}
std::shared_ptr<Result> decodeFile(const Request& request,const Cancel& cancelled) {
    @autoreleasepool { @try {
        checkCancel(cancelled);
        auto result=std::make_shared<Result>(); result->path=request.path; Table table(*result);
        struct stat fs{};
        if(stat(request.path.c_str(),&fs)||!S_ISREG(fs.st_mode)) throw std::runtime_error("Select an existing regular local image file");
        auto path=std::filesystem::path(request.path);
        table.add("file.path",request.path,"File","Filesystem");
        table.add("file.name",path.filename().string(),"File","Filesystem");
        table.add("file.extension",path.extension().string(),"File","Filesystem");
        table.add("file.size_bytes",std::to_string(fs.st_size),"File","Filesystem");
        table.add("file.modified_unix_seconds",number(fs.st_mtimespec.tv_sec+fs.st_mtimespec.tv_nsec/1e9),"File","Filesystem");
        table.add("file.created_unix_seconds",number(fs.st_birthtimespec.tv_sec+fs.st_birthtimespec.tv_nsec/1e9),"File","Filesystem");
        table.add("file.changed_unix_seconds",number(fs.st_ctimespec.tv_sec+fs.st_ctimespec.tv_nsec/1e9),"File","Filesystem");
        NSURL* url=[NSURL fileURLWithPath:[NSString stringWithUTF8String:request.path.c_str()]];
        Owned<CGImageSourceRef> source(CGImageSourceCreateWithURL((__bridge CFURLRef)url,(__bridge CFDictionaryRef)@{(__bridge id)kCGImageSourceShouldCache:@NO}));
        if(!source.p) throw std::runtime_error("Image I/O could not open this image format");
        size_t count=CGImageSourceGetCount(source);
        if(!count||count>10000) throw std::runtime_error("Image source must contain 1 to 10000 images");
        auto type=CGImageSourceGetType(source);
        table.add("imageio.type",str(type),"ImageIO","ImageIO");
        table.add("imageio.image_count",std::to_string(count),"ImageIO","ImageIO");
        result->primary=std::min(CGImageSourceGetPrimaryImageIndex(source),count-1);
        result->selected=result->primary;
        if(request.image!="primary"&&!request.image.empty()) {
            try { size_t n=0; auto i=std::stoull(request.image,&n); if(n!=request.image.size()||i>=count) throw std::out_of_range("index"); result->selected=size_t(i); }
            catch(...) { result->selectionFallback=true;table.warning("Selected image is no longer available; using the primary image"); }
        }
        Owned<CFDictionaryRef> properties(CGImageSourceCopyProperties(source,nullptr));
        flatten(properties,"imageio.container","Container",table);
        int orientation=1;
        for(size_t i=0;i<count;++i) {
            checkCancel(cancelled);
            Owned<CFDictionaryRef> p(CGImageSourceCopyPropertiesAtIndex(source,i,nullptr));
            uint64_t w=integer(p,kCGImagePropertyPixelWidth),h=integer(p,kCGImagePropertyPixelHeight);
            if(w>UINT32_MAX||h>UINT32_MAX) throw std::runtime_error("Image dimensions overflow");
            std::string key="imageio.images["+std::to_string(i)+"]";
            flatten(p,key,"Image",table);
            ImageEntry entry{i,uint32_t(w),uint32_t(h),std::to_string(i)+": "+std::to_string(w)+" x "+std::to_string(h)};
            if(i==result->primary) entry.label+=" (Primary)";
            bool left=false,right=false,mono=false;
            stereoRoles(properties,i,left,right,mono);stereoRoles(p,i,left,right,mono);
            if(left)entry.label+=" (Left)";if(right)entry.label+=" (Right)";if(mono)entry.label+=" (Monoscopic)";
            result->images.push_back(std::move(entry));
            if(i==result->selected) orientation=int(integer(p,kCGImagePropertyOrientation,1));
            if(!table.full()) { Owned<CGImageMetadataRef> metadata(CGImageSourceCopyMetadataAtIndex(source,i,nullptr));flatten(metadata,key+".tags","Metadata",table); }
        }
        if(orientation<1||orientation>8) { orientation=1;table.warning("Invalid image orientation; using upright"); }
        // Reject excessive source dimensions before asking Apple's decoder to allocate pixels.
        const auto& selected=result->images[result->selected]; imageElements(selected.width,selected.height,4);
        NSDictionary* options=@{(__bridge id)kCGImageSourceShouldAllowFloat:@YES,
            (__bridge id)kCGImageSourceShouldCacheImmediately:@YES,
            (__bridge id)kCGImageSourceDecodeRequest:(__bridge id)(request.decode=="sdr"?kCGImageSourceDecodeToSDR:kCGImageSourceDecodeToHDR)};
        Owned<CGImageRef> cg(CGImageSourceCreateImageAtIndex(source,result->selected,(__bridge CFDictionaryRef)options));
        if(!cg.p) throw std::runtime_error("Image I/O could not decode the selected image on this macOS version");
        checkCancel(cancelled);
        Owned<CGColorSpaceRef> linear(CGColorSpaceCreateWithName(kCGColorSpaceExtendedLinearSRGB));
        CIContext* context=[CIContext contextWithOptions:@{kCIContextWorkingColorSpace:(__bridge id)linear.p,kCIContextOutputColorSpace:(__bridge id)linear.p,kCIContextCacheIntermediates:@NO}];
        CIImage* image=[[CIImage imageWithCGImage:cg] imageByApplyingOrientation:orientation];
        if(!image||!context) throw std::runtime_error("Could not initialize color conversion");
        CGRect extent=image.extent;
        Plane color; color.width=uint32_t(extent.size.width);color.height=uint32_t(extent.size.height);color.channels=4;
        auto colorPixels=std::make_shared<std::vector<float>>(imageElements(color.width,color.height,4));
        color.pixels=colorPixels;
        [context render:image toBitmap:colorPixels->data() rowBytes:size_t(color.width)*4*sizeof(float) bounds:extent format:kCIFormatRGBAf colorSpace:linear];
        // Core Image bitmap rendering returns top-first rows. The orientation
        // transform has already been applied; only change row order here.
        orientTopLeft(color,1);
        result->planes[0]=std::move(color);
        table.add("decode.request",request.decode,"Decode","Plugin");
        table.add("decode.color_space","extended_linear_srgb","Decode","CoreImage");
        table.add("decode.alpha","premultiplied","Decode","CoreImage");
        table.add("decode.source_bits_per_component",std::to_string(CGImageGetBitsPerComponent(cg)),"Decode","ImageIO");
        table.add("decode.selected_image",std::to_string(result->selected),"Decode","Plugin");
        table.add("decode.primary_image",std::to_string(result->primary),"Decode","ImageIO");
        table.add("decode.orientation_applied",std::to_string(orientation),"Decode","Plugin");
        size_t total=result->planes[0].pixels->size()*sizeof(float),retainedTotal=0;
        for(size_t slot=1;slot<BufferCount;++slot) {
            checkCancel(cancelled);
            auto type=auxiliaryType(slot); if(!type) continue;
            Owned<CFDictionaryRef> dictionary(CGImageSourceCopyAuxiliaryDataInfoAtIndex(source,result->selected,type));
            if(!dictionary.p) continue;
            std::string key="imageio.images["+std::to_string(result->selected)+"].auxiliary."+Kinds[slot];
            flatten(dictionary,key,"Auxiliary",table);
            try { @try {
                Plane p;size_t retainedBytes=0;
                NSDictionary* d=(__bridge NSDictionary*)dictionary.p;
                NSError* error=nil;
                std::function<Plane(const Cancel&)> prepare;
                if(slot<=2) {
                    AVDepthData* depth=[AVDepthData depthDataFromDictionaryRepresentation:d error:&error];
                    if(!depth) throw std::runtime_error(error.localizedDescription.UTF8String?:"Invalid depth data");
                    // Convert precision only, never convert between depth and disparity.
                    OSType target=slot==1?kCVPixelFormatType_DepthFloat32:kCVPixelFormatType_DisparityFloat32;
                    if(![depth.availableDepthDataTypes containsObject:@(target)]) throw std::runtime_error("Depth precision conversion unavailable");
                    depth=[[depth depthDataByConvertingToDepthDataType:target] depthDataByApplyingExifOrientation:(CGImagePropertyOrientation)orientation];
                    p=pixelBuffer(depth.depthDataMap,false);
                    retainedBytes=CVPixelBufferGetDataSize(depth.depthDataMap);
                    prepare=[depth](const Cancel& cancel) {
                        @autoreleasepool { checkCancel(cancel);
                            auto ready=pixelBuffer(depth.depthDataMap); orientTopLeft(ready,1); return ready; }
                    };
                    calibration(depth,key,table);
                    table.add(key+".units",slot==1?"meters":"1/meters","Depth","AVFoundation");
                    table.add(key+".distance_calibrated",depth.depthDataAccuracy==AVDepthDataAccuracyAbsolute?"true":"false","Depth","AVFoundation");
                } else {
                    // Image I/O exposes unpacked auxiliary rasters directly. Reading
                    // them preserves all supported matte/gain types, including sky
                    // payloads that AVSemanticSegmentationMatte declines to wrap.
                    if(slot>=9&&!CFDictionaryContainsKey(dictionary,kCGImageAuxiliaryDataInfoData)) {
                        // Core Image prefers ISO when both types exist. Never
                        // substitute that image into the permanent Apple slot.
                        if(slot==9) {if(@available(macOS 15.0,*)) {
                            Owned<CFDictionaryRef> iso(CGImageSourceCopyAuxiliaryDataInfoAtIndex(source,result->selected,kCGImageAuxiliaryDataTypeISOGainMap));
                            if(iso.p)throw std::runtime_error("Apple gain raster unavailable; ISO map kept in its own output");
                        }}
                        // Render once to verify this special Image I/O payload.
                        // It has no raw bytes to validate/cache without Core Image.
                        auto decoded=planarGainMap(source,result->selected,dictionary);
                        retainedBytes=decoded.pixels->size()*sizeof(float);
                        p=decoded;p.pixels.reset();
                        prepare=[decoded,orientation](const Cancel& cancel) {
                            checkCancel(cancel); auto ready=decoded; orientTopLeft(ready,orientation); return ready;
                        };
                    } else {
                        p=rawAuxiliary(dictionary,false);
                        retainedBytes=CFDataGetLength((CFDataRef)CFDictionaryGetValue(dictionary,kCGImageAuxiliaryDataInfoData));
                        auto retained=std::shared_ptr<const void>(CFRetain(dictionary.p),[](const void* value){CFRelease(value);});
                        prepare=[retained,orientation](const Cancel& cancel) {
                            checkCancel(cancel); auto ready=rawAuxiliary((CFDictionaryRef)retained.get());
                            orientTopLeft(ready,orientation); return ready;
                        };
                    }
                    if(orientation>=5)std::swap(p.width,p.height);
                    if(slot<=8 && p.channels!=1) throw std::runtime_error("Matte must have one component");
                }
                size_t bytes=imageElements(p.width,p.height,p.channels)*sizeof(float);
                if(total>MaxImageBytes-bytes) throw std::runtime_error("Combined image data exceeds 1 GiB");
                if(retainedBytes>MaxImageBytes || retainedTotal>MaxImageBytes-retainedBytes)throw std::runtime_error("Cached native auxiliary data exceeds 1 GiB");
                total+=bytes;retainedTotal+=retainedBytes;
                if(request.auxiliaryMask&(1u<<slot)) {p=prepare(cancelled);++result->preparations;}
                else result->prepare[slot]=[prepare](const Cancel& cancel) {
                    @autoreleasepool { @try {return prepare(cancel);}
                    @catch(NSException* exception) {throw std::runtime_error(exception.reason.UTF8String?:"Apple auxiliary preparation exception");} }
                };
                result->planes[slot]=std::move(p);
            } @catch(NSException* exception) { throw std::runtime_error(exception.reason.UTF8String?:"Apple auxiliary decoder exception"); } }
            catch(const std::exception& e) { table.warning(std::string(Labels[slot])+": "+e.what()); }
        }
        for(size_t slot=0;slot<BufferCount;++slot) {
            auto& p=result->planes[slot]; if(!p.available()) continue;
            std::string key=std::string("outputs.")+Kinds[slot];
            table.add(key+".buffer_index",std::to_string(slot),"Output","Plugin");
            table.add(key+".width",std::to_string(p.width),"Output","Plugin");
            table.add(key+".height",std::to_string(p.height),"Output","Plugin");
            table.add(key+".source_channels",std::to_string(p.channels),"Output","Plugin");
            table.add(key+".available","true","Output","Plugin");
        }
        checkCancel(cancelled); table.finish();return result;
    } @catch(NSException* exception) { throw std::runtime_error(exception.reason.UTF8String?:"Apple image decoder exception"); } }
}
}
