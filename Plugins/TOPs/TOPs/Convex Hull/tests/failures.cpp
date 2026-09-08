#include "HullEngine.h"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>

// One-shot allocation failure, including failures in background channel jobs.
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
    std::vector<float> input(67*65*4);
    for (size_t i = 0; i < input.size(); ++i) input[i] = (i*17+i/19)%7 > 2;
    unsigned failures = 0;
    for (long budget = 0; budget < 180; ++budget)
    {
        Hull::Engine engine;
        Hull::Settings s; s.source = Hull::Source::RGBA; s.grouping = Hull::Grouping::PerBlob;
        countdown = budget;
        try { engine.process({input.data(),67,65,67*4},s); }
        catch (const std::bad_alloc&)
        {
            ++failures;
            if (!engine.measurements().empty()) { countdown = -1; return 4; }
        }
        catch (...) { countdown = -1; std::cerr << "Unexpected failure type\n"; return 1; }
        countdown = -1;
        // Recovery reuses the same instance, including any partially created pool.
        const auto result = engine.process({input.data(),67,65,67*4},s);
        if (!result.channels[3]) return 2;
    }
    if (failures < 30) return 3;
    std::cout << "Safely propagated and recovered from " << failures << " injected allocation failures\n";
}
