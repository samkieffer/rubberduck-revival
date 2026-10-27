#include "rd/Mixing.h"

#include <cstring>

#include "rd/Numeric.h"

namespace rd {
namespace mix {

namespace {

// pmulhw/pmullw/paddw on one 16-bit lane.
inline int16_t pmulhw(int16_t a, int16_t b) { return static_cast<int16_t>((static_cast<int32_t>(a) * b) >> 16); }
inline int16_t pmullw(int16_t a, int16_t b) { return static_cast<int16_t>(static_cast<int32_t>(a) * b); }
inline int16_t paddw(int16_t a, int16_t b) { return static_cast<int16_t>(a + b); }
inline int16_t paddsw(int16_t a, int16_t b) { return saturate16(static_cast<int32_t>(a) + b); }

// The original splits a 16.16 weight w into (w << 1) = hi:lo and computes
// x * w as pmulhw(x, lo >> 1) + pmullw(x >> 1, hi).
struct Weight {
    int16_t loHalf, hi;
    explicit Weight(int32_t w)
    {
        uint32_t s = static_cast<uint32_t>(w) << 1;
        loHalf = static_cast<int16_t>((s & 0xffff) >> 1);
        hi = static_cast<int16_t>(s >> 16);
    }
    int16_t apply(int16_t x) const
    {
        return paddw(pmulhw(x, loHalf), pmullw(static_cast<int16_t>(x >> 1), hi));
    }
};

} // namespace

void attenuate(int16_t* buf, unsigned n, float g)
{
    Weight w(ftol(static_cast<double>(g) * 65536.0f));
    for (unsigned i = 0; i < (n & ~3u); ++i)
        buf[i] = w.apply(buf[i]);
}

void amplify(int16_t* buf, unsigned n, float g)
{
    if (!(g > 1.0f)) {
        attenuate(buf, n, g);
        return;
    }
    Weight w(ftol(static_cast<double>(g) * 65536.0f));
    double inv = 1.0f / static_cast<double>(g);
    int16_t hiThr = static_cast<int16_t>(ftol(32767.0f * inv));
    int16_t loThr = static_cast<int16_t>(ftol(inv * -32768.0f));
    for (unsigned i = 0; i < (n & ~3u); ++i) {
        int16_t x = buf[i];
        uint16_t over = x > hiThr ? 0xffff : 0;
        uint16_t under = x > loThr ? 0 : 0xffff;
        uint16_t mask = over | under;
        int16_t v = static_cast<int16_t>(x & ~mask);
        uint16_t clip = static_cast<uint16_t>((over >> 1) | static_cast<uint16_t>(under << 15));
        buf[i] = static_cast<int16_t>(static_cast<uint16_t>(w.apply(v)) | clip);
    }
}

void applyGain(int16_t* buf, unsigned n, uint32_t& gain)
{
    if (gain == 0x8000)
        return;
    float g = static_cast<float>(static_cast<double>(gain) * (1.0f / 32768.0f));
    if (g > 1.0f)
        amplify(buf, n, g);
    else if (g == 0.0f)
        std::memset(buf, 0, n * sizeof(int16_t));
    else
        attenuate(buf, n, g);
    gain = 0x8000;
}

void mixInto(int16_t* dst, uint32_t& dstGain, const int16_t* src, uint32_t& srcGain, unsigned n)
{
    if (srcGain != 0) {
        if (dstGain == 0) {
            std::memcpy(dst, src, n * sizeof(int16_t));
        } else {
            double total = static_cast<double>(dstGain) + srcGain;
            Weight wd(ftol(static_cast<double>(dstGain) * 65536.0f / total));
            Weight ws(ftol(static_cast<double>(srcGain) * 65536.0f / total));
            for (unsigned i = 0; i < (n & ~3u); ++i)
                dst[i] = paddsw(wd.apply(dst[i]), ws.apply(src[i]));
        }
    }
    dstGain += srcGain;
    srcGain = 0;
}

void halve(int16_t* buf, unsigned n)
{
    for (unsigned i = 0; i < n; ++i)
        buf[i] = static_cast<int16_t>(buf[i] / 2);
}

} // namespace mix
} // namespace rd
