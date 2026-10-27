#pragma once

#include <cstdint>

namespace rd {

// Chamberlin state-variable filter (original class with vtable 0x435904).
// Within one process() call the original keeps the integrator states in x87
// registers and only rounds them to float at the end of the block; this is
// reproduced here.
class StateVariableFilter
{
public:
    enum Mode : int32_t { LowPass = 0, HighPass = 1, BandPass = 2, DoubleLowPass = 3 };

    // Original 0x40d5b0.
    void configure(Mode mode, int32_t sampleRate, float frequency, float damping);
    // Original 0x40d5e0: frequency in Hz relative to the configured rate.
    void setFrequency(float hz);
    // Original 0x40d600: 0 gives the gentlest damping (0.5).
    void setDamping(float x);
    // Original 0x40d620.
    void reset();
    // Original 0x40d7f0 and the four mode routines 0x40d630..0x40d750.
    void process(int16_t* buf, unsigned n);

    float debugState(int i) const { return i == 0 ? band1 : i == 1 ? low1 : i == 2 ? band2 : low2; }
    float debugF() const { return f; }
    float debugQ() const { return q; }

private:
    Mode mode = LowPass;
    int32_t rate = 44100;
    float f = 0;
    float q = 0;
    float band1 = 0, low1 = 0, band2 = 0, low2 = 0;
};

} // namespace rd
