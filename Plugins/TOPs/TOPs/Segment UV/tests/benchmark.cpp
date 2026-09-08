#include "SegmentEngine.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <vector>
cv::Mat legacyProcess(const cv::Mat&, double, MethodMenuItems, int);
int main(int argc, char** argv)
{
    const int samples = argc > 1 ? std::max(5, std::atoi(argv[1])) : 21;
    const int w = 1920, h = 1080;
    std::cout << "fixture,segments,method,mode,samples,median_ms,p95_ms,table_bytes_upper_bound\n";
    for (int count : {1,100,1000,10000})
    {
        cv::Mat image(h,w,CV_32FC4,cv::Scalar(0));
        if (count == 1) image.setTo(cv::Scalar(0,0,0,1));
        else for (int i=0;i<count;++i)
        {
            int x=2+(i%160)*12, y=2+(i/160)*12;
            for (int yy=y; yy<y+4; ++yy) for(int xx=x;xx<x+4;++xx) image.at<cv::Vec4f>(yy,xx)[3]=1;
        }
        for (auto method : {MethodMenuItems::Closestsegmentpixeltocentroid,MethodMenuItems::Random})
        {
            std::vector<double> times[4]; size_t bytes[4]{};
            // Rotate order to spread warm-cache and thermal effects across modes.
            for (int trial=-3;trial<samples;++trial) for (int index=0;index<4;++index)
            {
                const int mode=(index+(trial+3)%4)%4;
                const auto start=std::chrono::steady_clock::now();
                {
                    Segment::InfoTable table;
                    cv::Mat result;
                    if(mode==0) result=legacyProcess(image,.5,method,1);
                    else
                    {
                        table.setDetail(Segment::DATDetail(mode-1));
                        result=Segment::process(image,.5,method,1,&table);
                        if(table.rowCount()!=count+1) return 2;
                    }
                    if(result.empty()) return 3;
                    // Memory traversal runs in an unmeasured warmup only.
                    if(trial==-3) bytes[mode]=table.retainedBytes();
                }
                // Include freeing the image and table, as each cook replaces them.
                const auto stop=std::chrono::steady_clock::now();
                if(trial>=0) times[mode].push_back(std::chrono::duration<double,std::milli>(stop-start).count());
            }
            const char* names[]={"legacy","basic","medium","maximum"};
            for(int mode=0;mode<4;++mode)
            {
                auto& t=times[mode]; std::sort(t.begin(),t.end());
                std::cout << (count==1?"full_frame":"separated_4x4") << ',' << count << ','
                    << (method==MethodMenuItems::Random?"random":"closest") << ',' << names[mode] << ',' << samples
                    << ',' << std::fixed << std::setprecision(4) << t[t.size()/2] << ',' << t[(t.size()*95-1)/100]
                    << ',' << bytes[mode] << '\n';
            }
        }
    }
}
