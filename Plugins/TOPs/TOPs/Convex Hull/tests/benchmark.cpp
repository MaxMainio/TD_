#include "HullEngine.h"
#include "OutputPacking.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <random>
#include <vector>

int main(int argc, char** argv)
{
    const int width = argc > 1 ? std::stoi(argv[1]) : 1920;
    const int height = argc > 2 ? std::stoi(argv[2]) : 1080;
    const int repeats = argc > 3 ? std::stoi(argv[3]) : 9;
    if (width <= 0 || height <= 0 || repeats <= 0) return 1;
    using Clock = std::chrono::steady_clock;
    const char* fixtures[] = {"solid", "shapes", "sparse", "noise"};
    const char* sources[] = {"luminance","red","green","blue","alpha","rgb","rgba"};
    const char* groups[] = {"global","per_blob","largest_blob"};
    std::cout << "fixture,width,height,source,group,process_ms,pack_ms,total_median_ms,total_p95_ms,retained_bytes\n";
    for (int fixture = 0; fixture < 4; ++fixture)
    {
        std::vector<float> input(size_t(width)*height*4);
        std::vector<uint8_t> packed(size_t(width)*height*4);
        std::mt19937 rng(9857);
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) for (int c = 0; c < 4; ++c)
        {
            const int bx = (x+c*17)%240, by = (y+c*13)%180;
            const bool value = fixture == 0 || (fixture == 1 && bx > 40 && bx < 170 && by > 30 && by < 120) ||
                (fixture == 2 && rng()%1000 < 5) || (fixture == 3 && rng()%100 < 50);
            input[(size_t(y)*width+x)*4+c] = value;
        }
        for (int source = 0; source < 7; ++source) for (int group = 0; group < 3; ++group)
        {
            Hull::Engine engine;
            Hull::Settings s; s.source = Hull::Source(source); s.grouping = Hull::Grouping(group);
            std::vector<double> process, pack, total;
            for (int i = -2; i < repeats; ++i)
            {
                const auto start = Clock::now();
                const auto image = engine.process({input.data(),size_t(width),size_t(height),size_t(width)*4},s);
                const auto middle = Clock::now();
                Hull::packImage(image,TD::OP_PixelFormat::RGBA8Fixed,packed.data(),packed.size());
                const auto end = Clock::now();
                if (i < 0) continue;
                process.push_back(std::chrono::duration<double,std::milli>(middle-start).count());
                pack.push_back(std::chrono::duration<double,std::milli>(end-middle).count());
                total.push_back(std::chrono::duration<double,std::milli>(end-start).count());
            }
            std::sort(process.begin(),process.end()); std::sort(pack.begin(),pack.end()); std::sort(total.begin(),total.end());
            std::cout << fixtures[fixture] << ',' << width << ',' << height << ',' << sources[source] << ',' << groups[group] << ','
                << process[repeats/2] << ',' << pack[repeats/2] << ',' << total[repeats/2] << ','
                << total[size_t(std::ceil(.95*repeats))-1] << ',' << engine.retainedBytes() << '\n';
        }
    }
}
