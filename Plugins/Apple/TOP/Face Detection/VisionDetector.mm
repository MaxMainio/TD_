#include "VisionDetector.h"
#import <Vision/Vision.h>
#import <CoreVideo/CoreVideo.h>
#include <cstring>
#include <stdexcept>

namespace FaceDetection
{
namespace
{
std::string message(NSString* text)
{ const char* s=text.UTF8String; return s ? s : "Unknown Apple Vision error"; }
struct Buffer
{
    CVPixelBufferRef value=nullptr;
    ~Buffer() { if(value) CVPixelBufferRelease(value); }
};
struct Lock
{
    CVPixelBufferRef value;
    explicit Lock(CVPixelBufferRef buffer):value(buffer)
    {
        if(CVPixelBufferLockBaseAddress(value,0)!=kCVReturnSuccess)
            throw std::runtime_error("Could not lock the Vision input buffer");
    }
    ~Lock() { CVPixelBufferUnlockBaseAddress(value,0); }
};
void appendRegion(Face& face, VNFaceLandmarkRegion2D* region, const char* name)
{
    const CGPoint* points=region.normalizedPoints;
    if(!points) return;
    for(NSUInteger i=0;i<region.pointCount;++i)
        face.landmarks.push_back({name,size_t(i),imagePoint(face.box,{points[i].x,points[i].y})});
}
}

struct VisionDetector::Impl
{
    CVPixelBufferPoolRef pool=nullptr;
    uint32_t width=0,height=0;
    VNDetectFaceLandmarksRequest* request=nil;
    ~Impl() { if(pool) CVPixelBufferPoolRelease(pool); }
    void prepare(uint32_t w,uint32_t h)
    {
        if(!request)
        {
            request=[[VNDetectFaceLandmarksRequest alloc] init];
            request.revision=VNDetectFaceLandmarksRequestRevision3;
            request.constellation=VNRequestFaceLandmarksConstellation76Points;
            if(!request) throw std::runtime_error("Could not create an Apple Vision face request");
        }
        if(pool && w==width && h==height) return;
        if(pool) { CVPixelBufferPoolRelease(pool); pool=nullptr; }
        NSDictionary* attributes=@{
            (NSString*)kCVPixelBufferWidthKey:@(w),
            (NSString*)kCVPixelBufferHeightKey:@(h),
            (NSString*)kCVPixelBufferPixelFormatTypeKey:@(kCVPixelFormatType_32BGRA),
            (NSString*)kCVPixelBufferIOSurfacePropertiesKey:@{}
        };
        if(CVPixelBufferPoolCreate(kCFAllocatorDefault,nullptr,(__bridge CFDictionaryRef)attributes,&pool)!=kCVReturnSuccess || !pool)
            throw std::runtime_error("Could not create the Vision input buffer pool");
        width=w; height=h;
    }
};

VisionDetector::VisionDetector():impl_(new Impl) {}
VisionDetector::~VisionDetector()=default;

std::vector<Face> VisionDetector::detect(const uint8_t* pixels,size_t bytes,uint32_t width,uint32_t height)
{
    if(!pixels || bytes<imageBytes(width,height,4)) throw std::invalid_argument("Invalid BGRA input for face detection");
    @autoreleasepool
    {
        @try
        {
            impl_->prepare(width,height);
            Buffer buffer;
            if(CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault,impl_->pool,&buffer.value)!=kCVReturnSuccess || !buffer.value)
                throw std::runtime_error("Could not allocate the Vision input buffer");
            {
                Lock lock(buffer.value);
                auto* target=static_cast<uint8_t*>(CVPixelBufferGetBaseAddress(buffer.value));
                size_t stride=CVPixelBufferGetBytesPerRow(buffer.value), rowBytes=size_t(width)*4;
                if(!target || stride<rowBytes) throw std::runtime_error("Invalid Vision input buffer layout");
                for(uint32_t y=0;y<height;++y) std::memcpy(target+y*stride,pixels+y*rowBytes,rowBytes);
            }
            VNImageRequestHandler* handler=[[VNImageRequestHandler alloc]
                initWithCVPixelBuffer:buffer.value orientation:kCGImagePropertyOrientationUp options:@{}];
            NSError* error=nil;
            if(![handler performRequests:@[impl_->request] error:&error])
                throw std::runtime_error("Apple Vision face detection failed: "+message(error.localizedDescription));
            std::vector<Face> faces;
            for(VNFaceObservation* observation in impl_->request.results)
            {
                Face face;
                CGRect box=observation.boundingBox;
                face.box={box.origin.x,box.origin.y,box.size.width,box.size.height};
                face.confidence=observation.confidence;
                VNFaceLandmarks2D* landmarks=observation.landmarks;
                appendRegion(face,landmarks.faceContour,"face_contour");
                appendRegion(face,landmarks.leftEye,"left_eye");
                appendRegion(face,landmarks.rightEye,"right_eye");
                appendRegion(face,landmarks.leftEyebrow,"left_eyebrow");
                appendRegion(face,landmarks.rightEyebrow,"right_eyebrow");
                appendRegion(face,landmarks.nose,"nose");
                appendRegion(face,landmarks.noseCrest,"nose_crest");
                appendRegion(face,landmarks.medianLine,"median_line");
                appendRegion(face,landmarks.outerLips,"outer_lips");
                appendRegion(face,landmarks.innerLips,"inner_lips");
                appendRegion(face,landmarks.leftPupil,"left_pupil");
                appendRegion(face,landmarks.rightPupil,"right_pupil");
                VNFaceLandmarkRegion2D* all=landmarks.allPoints;
                const CGPoint* points=all.normalizedPoints;
                if(points) for(NSUInteger i=0;i<all.pointCount;++i)
                    face.allPoints.push_back(imagePoint(face.box,{points[i].x,points[i].y}));
                faces.push_back(std::move(face));
            }
            return faces;
        }
        @catch(NSException* exception)
        { throw std::runtime_error("Apple Vision exception: "+message(exception.reason)); }
    }
}
}
