#include "DitherEngine.h"
#include "OutputPacking.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sys/resource.h>

using Clock = std::chrono::steady_clock;
double milliseconds(Clock::time_point from, Clock::time_point to)
{
    return std::chrono::duration<double,std::milli>(to-from).count();
}
double cpuMilliseconds()
{
    rusage usage{}; getrusage(RUSAGE_SELF,&usage);
    return (usage.ru_utime.tv_sec + usage.ru_stime.tv_sec)*1000.0 +
           (usage.ru_utime.tv_usec + usage.ru_stime.tv_usec)/1000.0;
}
double peakMiB()
{
    rusage usage{}; getrusage(RUSAGE_SELF,&usage);
#ifdef __APPLE__
    return usage.ru_maxrss / (1024.0*1024.0);
#else
    return usage.ru_maxrss / 1024.0;
#endif
}
double percentile(std::vector<double> values,double fraction)
{
    std::sort(values.begin(),values.end());
    return values[static_cast<size_t>(std::ceil(fraction*(values.size()-1)))];
}

int main(int argc,char** argv)
{
    const int samples = argc > 1 ? std::max(1,std::atoi(argv[1])) : 9;
    const unsigned workers = argc > 2 ? static_cast<unsigned>(std::max(0,std::atoi(argv[2]))) : 3;
    const char* names[] = {"Floyd-Steinberg","Jarvis-Judice-Ninke","Stucki","Atkinson","Burkes",
                          "Sierra Full","Sierra Two-Row","Sierra Lite"};
    std::cout << "width,height,kernel,color,samples,process_median_ms,pack_median_ms,total_median_ms,total_p95_ms,cpu_ms_per_frame,peak_rss_mib,retained_bytes,workers\n";
    std::cout << std::fixed << std::setprecision(3);
    for (const auto& size : {std::array<int,2>{512,512}, {1920,1080}})
    {
        const size_t w = size[0],h = size[1];
        std::vector<float> input(w*h*4);
        uint32_t rng = 713135;
        for (size_t y = 0; y < h; ++y)
            for (size_t x = 0; x < w; ++x)
            {
                rng = rng*1664525u+1013904223u;
                input[(y*w+x)*4] = static_cast<float>(x)/std::max<size_t>(1,w-1);
                input[(y*w+x)*4+1] = static_cast<float>(y)/std::max<size_t>(1,h-1);
                input[(y*w+x)*4+2] = static_cast<float>(rng>>8)/16777215.f;
                input[(y*w+x)*4+3] = .7f;
            }
        Dither::Engine engine(workers);
        std::vector<uint8_t> packed(w*h*4);
        for (int kernel = 0; kernel < 8; ++kernel)
            for (int mono = 0; mono < 2; ++mono)
            {
                Dither::Settings s; s.kernel = static_cast<Dither::Kernel>(kernel);
                s.color = mono ? Dither::Color::Monochrome : Dither::Color::RGB;
                std::vector<double> process,pack,total,cpu;
                for (int i = -2; i < samples; ++i)
                {
                    const double cpuStart = cpuMilliseconds();
                    const auto start = Clock::now();
                    const auto image = engine.process({input.data(),w,h,w*4},w,h,s);
                    const auto processed = Clock::now();
                    Dither::packImage(image,TD::OP_PixelFormat::RGBA8Fixed,packed.data(),packed.size());
                    const auto end = Clock::now();
                    if (i >= 0)
                    {
                        process.push_back(milliseconds(start,processed));
                        pack.push_back(milliseconds(processed,end));
                        total.push_back(milliseconds(start,end));
                        cpu.push_back(cpuMilliseconds()-cpuStart);
                    }
                }
                std::cout << w << ',' << h << ',' << names[kernel] << ',' << (mono ? "Monochrome" : "RGB")
                    << ',' << samples << ',' << percentile(process,.5) << ',' << percentile(pack,.5)
                    << ',' << percentile(total,.5) << ',' << percentile(total,.95)
                    << ',' << std::accumulate(cpu.begin(),cpu.end(),0.0)/samples
                    << ',' << peakMiB() << ',' << engine.retainedBytes() << ',' << engine.workerCount() << std::endl;
            }
    }
}
