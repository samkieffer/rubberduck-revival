#pragma once

#include <cstdint>

namespace rd {

// Table waveshaper / saturating low-pass (original class with vtable 0x435a38).
//   mode 0: odd-symmetric clipper (Filter Drive, Tweak off)
//   mode 1: even-symmetric (rectifying) clipper + DC blocker (Tweak on)
//   mode 2: clipper with random gain modulation
//   mode 3: clipping one-pole low-pass mixed with the dry signal (FX distortion)
class Shaper
{
public:
    Shaper();
    // Original 0x4108b0. For modes 0-2 `amount` sets the gain and `ceiling` the
    // clip level; for mode 3 `amount` is the wet mix and `ceiling` the smoothing.
    void configure(uint8_t mode, uint8_t amount, uint8_t ceiling);
    // Original 0x410ad0. Mode 2 draws from `rand`.
    void process(int16_t* buf, unsigned n, int (*rand)() = nullptr);

    const int16_t* table() const { return table_; }

    float debugState(int i) const { return i == 0 ? s208_ : i == 1 ? s20c_ : s210_; }

private:
    uint8_t mode_ = 0;
    uint8_t amount_ = 0;
    int16_t table_[257] = {};
    float s208_ = 0, s20c_ = 0, s210_ = 0;  // shared by modes 1 and 3, as in the original
};

} // namespace rd
