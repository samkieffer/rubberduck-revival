#pragma once

// Numeric helpers reproducing the arithmetic of the original MSVC 6 / x87 build.
//
// The original runs the x87 FPU with 53-bit precision, so intermediate results
// behave like IEEE doubles; values written back to memory as `float` are
// rounded to float at that point. The engine code mirrors this by computing in
// double and storing state in float exactly where the original stores floats.
// Build the core with -ffp-contract=off so no fused multiply-add changes the
// rounding.

#include <cmath>
#include <cstdint>

namespace rd {

// MSVC __ftol: convert with truncation toward zero (to 32-bit int).
inline int32_t ftol(double x) { return static_cast<int32_t>(x); }

// x87 fistp under the default rounding mode (round half to even).
inline int32_t fistp(double x) { return static_cast<int32_t>(std::nearbyint(x)); }

// Signed division by 2^n rounding toward zero, as emitted by MSVC for `x / 256`.
inline int32_t divPow2(int32_t x, int n) { return (x + ((x >> 31) & ((1 << n) - 1))) >> n; }

inline int16_t saturate16(int32_t x)
{
    return static_cast<int16_t>(x > 32767 ? 32767 : (x < -32768 ? -32768 : x));
}

} // namespace rd
