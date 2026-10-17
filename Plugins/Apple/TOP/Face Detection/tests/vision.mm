#include "VisionDetector.h"
#include <iostream>
#include <stdexcept>

int main()
{
    try
    {
        FaceDetection::VisionDetector detector;
        // Exercise real Vision requests, pooled input reuse and resolution change.
        for(uint32_t size:{256,256,320})
        {
            std::vector<uint8_t> pixels(size_t(size)*size*4,0);
            for(size_t i=3;i<pixels.size();i+=4) pixels[i]=255;
            if(!detector.detect(pixels.data(),pixels.size(),size,size).empty())
                throw std::runtime_error("Unexpected face in a blank image");
        }
        bool rejected=false;
        try { detector.detect(nullptr,0,256,256); } catch(const std::invalid_argument&) { rejected=true; }
        if(!rejected) throw std::runtime_error("Invalid input was not rejected");
        std::cout<<"Apple Vision revision 3 / 76-point requests, pool reuse and resize passed\n";
    }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
