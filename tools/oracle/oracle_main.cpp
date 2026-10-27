// Oracle tests: run original rdh3o.exe DSP routines side by side with the
// re-implementation in core/ and compare outputs sample by sample.
//
// Build (see tools/oracle/build.sh) and run under Wine:
//   wine oracle.exe path/to/rdh3o.exe [test-name]
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>
#include <string>

extern "C" {
#include "rdimage.h"
}

#include "rd/LadderFilter.h"

#define THISCALL __attribute__((thiscall))

namespace {

template <typename T> T fn(uint32_t addr) { return reinterpret_cast<T>(addr); }

void setFpuLikeOriginal()
{
    // 53-bit precision, round to nearest, all exceptions masked (MSVC default).
    unsigned short cw = 0x027f;
    __asm__ volatile("fldcw %0" : : "m"(cw));
}

struct Lcg {
    uint32_t s;
    explicit Lcg(uint32_t seed) : s(seed) {}
    uint32_t next() { s = s * 1664525u + 1013904223u; return s; }
    int16_t sample() { return static_cast<int16_t>(next() >> 16); }
    float unit() { return (next() >> 8) * (1.0f / 16777216.0f); }
};

// Test signal: saw + square + noise bursts, full 16-bit range.
std::vector<int16_t> testSignal(unsigned n, uint32_t seed)
{
    Lcg rng(seed);
    std::vector<int16_t> v(n);
    uint32_t phase = 0;
    for (unsigned i = 0; i < n; ++i) {
        uint32_t inc = 40000000u + (i / 512) * 3000000u;
        phase += inc;
        int32_t s = static_cast<int16_t>(phase >> 16);
        if ((i / 2048) % 3 == 1)
            s = (phase & 0x80000000u) ? 30000 : -30000;
        else if ((i / 2048) % 3 == 2)
            s = rng.sample();
        v[i] = static_cast<int16_t>(s);
    }
    return v;
}

struct Result {
    bool ok = true;
    unsigned mismatches = 0;
    int maxDiff = 0;
    long firstIndex = -1;
};

Result compare(const std::vector<int16_t>& a, const std::vector<int16_t>& b)
{
    Result r;
    for (size_t i = 0; i < a.size(); ++i) {
        int d = std::abs(a[i] - b[i]);
        if (d) {
            if (r.firstIndex < 0) r.firstIndex = static_cast<long>(i);
            r.mismatches++;
            if (d > r.maxDiff) r.maxDiff = d;
        }
    }
    r.ok = r.mismatches == 0;
    return r;
}

bool report(const char* name, const Result& r, size_t n)
{
    std::printf("%-28s %s  samples=%zu mismatches=%u maxdiff=%d first=%ld\n", name,
                r.ok ? "OK  " : "FAIL", n, r.mismatches, r.maxDiff, r.firstIndex);
    return r.ok;
}

// ---------------------------------------------------------------------------

bool testLadder()
{
    typedef void(THISCALL * Ctor)(void*);
    typedef void(THISCALL * Set2)(void*, float, float);
    typedef void(THISCALL * Reset)(void*);
    typedef void(THISCALL * Process)(void*, int16_t*, unsigned);
    auto ctor = fn<Ctor>(0x40d870);
    auto set = fn<Set2>(0x40d890);
    auto setCut = fn<void(THISCALL*)(void*, float)>(0x40d8b0);
    auto setRes = fn<void(THISCALL*)(void*, float)>(0x40d8d0);
    auto reset = fn<Reset>(0x40d900);
    auto process = fn<Process>(0x40d930);

    alignas(16) uint8_t orig[0x40] = {};
    ctor(orig);
    set(orig, 0.5f, 0.0f);
    reset(orig);

    rd::LadderFilter mine;
    mine.setCutoff(0.5f);
    mine.setResonance(0.0f);
    mine.reset();

    const unsigned n = 128 * 400;
    std::vector<int16_t> a = testSignal(n, 1234), b = a;
    Lcg params(99);
    for (unsigned blk = 0; blk < n / 128; ++blk) {
        float cut = params.unit();
        float res = params.unit();
        setCut(orig, cut);
        setRes(orig, res);
        mine.setCutoff(cut);
        mine.setResonance(res);
        process(orig, &a[blk * 128], 128);
        mine.process(&b[blk * 128], 128);
    }
    return report("ladder filter", compare(a, b), n);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: oracle rdh3o.exe [test]\n");
        return 2;
    }
    if (!rd_load(argv[1]))
        return 1;
    setFpuLikeOriginal();
    std::string only = argc > 2 ? argv[2] : "";
    bool ok = true;
    if (only.empty() || only == "ladder") ok &= testLadder();
    return ok ? 0 : 1;
}
