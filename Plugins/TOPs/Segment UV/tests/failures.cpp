#include "SegmentEngine.h"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>

// One-shot failures in C++ allocations; this does not intercept OpenCV malloc.
// Single-threaded OpenCV avoids throwing through third-party worker boundaries.
static std::atomic<long> countdown{-1};
void* operator new(size_t bytes)
{
    long count = countdown.load();
    while (count >= 0)
    {
        if (countdown.compare_exchange_weak(count,count-1))
        { if (count == 0) throw std::bad_alloc(); break; }
    }
    if (void* p = std::malloc(bytes ? bytes : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](size_t bytes) { return ::operator new(bytes); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }

int main()
{
    cv::setNumThreads(1);
    cv::Mat input(31,43,CV_32FC4,cv::Scalar(0));
    for(int y=1;y<30;y+=3) for(int x=1;x<42;x+=3) input.at<cv::Vec4f>(y,x)[3]=1;
    Segment::InfoTable warmup;
    Segment::process(input,.5,MethodMenuItems::Centroid,1,&warmup);
    unsigned failures=0;
    for(long budget=0;budget<360;++budget)
    {
        Segment::InfoTable table;
        table.setDetail(Segment::DATDetail::Maximum);
        countdown=budget;
        try { Segment::process(input,.5,MethodMenuItems::Medianpixelcoordinate,1,&table); }
        catch (const std::bad_alloc&)
        {
            ++failures;
            countdown=-1;
            if(table.rowCount()!=1 || table.columnCount()!=15) return 2;
        }
        catch (...) { countdown=-1; std::cerr << "Unexpected failure type\n"; return 3; }
        countdown=-1;
        Segment::process(input,.5,MethodMenuItems::Medianpixelcoordinate,1,&table);
        if(table.rowCount()!=141) return 4;
    }
    if(failures<50) return 5;
    std::cout << failures << " injected C++ allocation failures cleared rows and recovered\n";
}
