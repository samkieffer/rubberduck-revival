#pragma once

#include <cstdint>

namespace rd {

// Fixed-point gain stages of the original audio server ("DAS"), reproducing
// its MMX code paths (0x40fe70, 0x40fc40, 0x410000, 0x410210) lane by lane.
// Gains are 16.16-style: 0x8000 means unity for a track gain.
namespace mix {

// Scale by g < = 1 (original 0x40fe70, MMX path).
void attenuate(int16_t* buf, unsigned n, float g);
// Scale by g > 1 with clipping at the 16-bit range (original 0x40fc40, MMX path).
void amplify(int16_t* buf, unsigned n, float g);
// Bring a track to unity gain: scales the samples by gain / 0x8000 and resets
// the gain (original 0x410210).
void applyGain(int16_t* buf, unsigned n, uint32_t& gain);
// Weighted sum of two tracks into `dst`, weights normalised by the gain sum;
// the gains are then added (original 0x410000 + 0x411140).
void mixInto(int16_t* dst, uint32_t& dstGain, const int16_t* src, uint32_t& srcGain, unsigned n);
// Halve with truncation toward zero (original 0x40ff70).
void halve(int16_t* buf, unsigned n);

} // namespace mix
} // namespace rd
