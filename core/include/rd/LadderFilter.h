#pragma once

#include <cstdint>

namespace rd {

// Four-stage low-pass used when "Filter Modification" is off
// (original class with vtable 0x435920, process routine 0x40d930).
class LadderFilter
{
public:
    // Original 0x40d8b0: cutoff in 0..1 (not Hz).
    void setCutoff(float x);
    // Original 0x40d8d0: resonance in 0..1, shaped by the current cutoff.
    void setResonance(float r);
    // Original 0x40d900.
    void reset();
    // Original 0x40d930: in-place on 16-bit samples.
    void process(int16_t* buf, unsigned n);

private:
    float x1 = 0, x2 = 0, x3 = 0, x4 = 0;   // previous stage inputs
    float pState = 0;                        // smoothed coefficient
    float cutoff = 0;
    float y1 = 0, y2 = 0, y3 = 0, y4 = 0;   // stage outputs
    float rState = 0;                        // smoothed resonance
    float feedback = 0;
    float pTarget = 0;
    float rTarget = 0;
};

} // namespace rd
