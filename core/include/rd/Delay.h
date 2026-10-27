#pragma once

#include <cstdint>
#include <vector>

namespace rd {

// Mono feedback delay (original class with vtable 0x4358f4).
class Delay
{
public:
    // Original 0x40d3f0: allocates `maxMs` of buffer at `rate`.
    void init(uint32_t maxMs, uint16_t rate);
    // Original 0x40d440: gain and feedback are 0..1 floats (scaled by 256 internally).
    void set(uint32_t ms, float gain, float feedback);
    // Original 0x40d4a0.
    void clear();
    // Original 0x40d4d0.
    void process(int16_t* buf, unsigned n);

    uint32_t debugPos() const { return pos; }
    uint32_t debugLength() const { return length; }

private:
    std::vector<int16_t> line;
    uint32_t length = 0;
    uint32_t pos = 0;
    uint16_t rate = 44100;
    int32_t gain = 0;
    int32_t feedback = 0;
};

} // namespace rd
