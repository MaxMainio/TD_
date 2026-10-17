#include "DitherEngine.h"
#include <cstdlib>
#include <iostream>
#include <vector>

// Binary pipe used only by python_reference.py: RGBA32 input, R32 output.
int main(int argc, char** argv)
{
    if (argc != 5) return 2;
    try
    {
        const size_t w = std::stoul(argv[3]), h = std::stoul(argv[4]);
        if (!w || !h || w > 4096 || h > 4096) return 2;
        std::vector<float> input(w*h*4);
        if (!std::cin.read(reinterpret_cast<char*>(input.data()),input.size()*sizeof(float))) return 2;
        Dither::Settings settings;
        settings.kernel = static_cast<Dither::Kernel>(std::stoi(argv[1]));
        settings.bits = std::stoi(argv[2]);
        Dither::Engine engine(0);
        const auto output = engine.process({input.data(),w,h,w*4},w,h,settings,1);
        std::cout.write(reinterpret_cast<const char*>(output.channels[0]),w*h*sizeof(float));
        return std::cout ? 0 : 2;
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 2; }
}
