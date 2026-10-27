#pragma once

#include <cstdint>

namespace rd {

// MSVC CRT rand(): the original's noise oscillator, pitch drift and shuffle
// mode all draw from it, in a fixed order per block.
class Random
{
public:
    explicit Random(uint32_t seed = 1) : state(seed) {}
    void seed(uint32_t s) { state = s; }
    int next()
    {
        state = state * 214013u + 2531011u;
        return static_cast<int>((state >> 16) & 0x7fff);
    }

private:
    uint32_t state;
};

} // namespace rd
