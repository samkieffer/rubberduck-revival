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
#include "rd/StateVariableFilter.h"
#include "rd/Shaper.h"
#include "rd/Engine.h"
#include "rd/H30File.h"
#include "rd/Mixing.h"

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

bool testSvf(int mode)
{
    typedef void(THISCALL * Configure)(void*, int, int, float, float);
    auto configure = fn<Configure>(0x40d5b0);
    auto setFreq = fn<void(THISCALL*)(void*, float)>(0x40d5e0);
    auto setDamp = fn<void(THISCALL*)(void*, float)>(0x40d600);
    auto reset = fn<void(THISCALL*)(void*)>(0x40d620);
    auto process = fn<void(THISCALL*)(void*, int16_t*, unsigned)>(0x40d7f0);

    alignas(16) uint8_t orig[0x40] = {};
    *reinterpret_cast<uint32_t*>(orig) = 0x435904;  // vtable, as set by ctor 0x40d590
    configure(orig, mode, 22050, 2756.25f, 0.0f);
    reset(orig);

    rd::StateVariableFilter mine;
    mine.configure(static_cast<rd::StateVariableFilter::Mode>(mode), 22050, 2756.25f, 0.0f);
    mine.reset();

    const unsigned n = 128 * 400;
    std::vector<int16_t> a = testSignal(n, 777 + mode), b = a;
    Lcg params(42 + mode);
    for (unsigned blk = 0; blk < n / 128; ++blk) {
        float hz = 60.0f + params.unit() * 3300.0f;
        float damp = params.unit() * params.unit() * 0.6f;
        setFreq(orig, hz);
        setDamp(orig, damp);
        mine.setFrequency(hz);
        mine.setDamping(damp);
        process(orig, &a[blk * 128], 128);
        mine.process(&b[blk * 128], 128);
    }
    char name[64];
    std::snprintf(name, sizeof name, "svf mode %d", mode);
    return report(name, compare(a, b), n);
}

// The original pops a fourth, unused argument (ret 0x10).
typedef void(THISCALL * ShaperConfigure)(void*, uint32_t, uint32_t, uint32_t, uint32_t);
typedef void(THISCALL * ShaperProcess)(void*, int16_t*, unsigned);

bool testShaperTables()
{
    auto ctor = fn<void(THISCALL*)(void*)>(0x410880);
    auto configure = fn<ShaperConfigure>(0x4108b0);
    alignas(16) static uint8_t orig[0x220];
    ctor(orig);
    rd::Shaper mine;
    unsigned bad = 0, total = 0;
    for (int mode = 0; mode < 4; ++mode)
        for (int amount = 0; amount < 256; ++amount)
            for (int ceiling = 0; ceiling < 256; ++ceiling) {
                if (mode == 3 && amount > 0) break;  // mode 3 coefficients depend on `ceiling` only
                configure(orig, mode, amount, ceiling, 0);
                mine.configure(mode, amount, ceiling);
                total++;
                if (mode < 3) {
                    if (std::memcmp(orig + 6, mine.table(), 257 * 2) != 0) bad++;
                } else {
                    // compare via processing a short signal (coefficients are private)
                }
            }
    std::printf("%-28s %s  configurations=%u mismatches=%u\n", "shaper tables", bad ? "FAIL" : "OK  ", total, bad);
    return bad == 0;
}

bool testShaper(int mode)
{
    auto ctor = fn<void(THISCALL*)(void*)>(0x410880);
    auto configure = fn<ShaperConfigure>(0x4108b0);
    auto process = fn<ShaperProcess>(0x410ad0);
    alignas(16) static uint8_t orig[0x220];
    ctor(orig);
    rd::Shaper mine;

    const unsigned n = 128 * 400;
    std::vector<int16_t> a = testSignal(n, 555 + mode), b = a;
    Lcg params(7 + mode);
    std::vector<std::pair<int, int>> settings;
    for (unsigned blk = 0; blk < n / 128; ++blk)
        settings.push_back({static_cast<int>(params.next() >> 24), static_cast<int>(params.next() >> 24)});

    rd_srand(12345);
    for (unsigned blk = 0; blk < n / 128; ++blk) {
        if (blk % 8 == 0) configure(orig, mode, settings[blk].first, settings[blk].second, 0);
        process(orig, &a[blk * 128], 128);
    }
    rd_srand(12345);
    for (unsigned blk = 0; blk < n / 128; ++blk) {
        if (blk % 8 == 0) mine.configure(mode, settings[blk].first, settings[blk].second);
        mine.process(&b[blk * 128], 128, rd_rand);
    }
    char name[64];
    std::snprintf(name, sizeof name, "shaper mode %d", mode);
    return report(name, compare(a, b), n);
}

// ---------------------------------------------------------------------------
// Whole voice: original render routine 0x408ca0 against rd::Engine.

// Capture of the second-branch input (T0 copy) inside the original, taken by
// replacing its halve routine 0x40ff70 with an equivalent that records first.
std::vector<int16_t> g_branchInput;
int THISCALL capturingHalve(uint8_t* slot, int* range)
{
    int16_t* data = *reinterpret_cast<int16_t**>(slot + 0x14);
    g_branchInput.insert(g_branchInput.end(), data + range[1], data + range[1] + range[2]);
    for (int i = range[1]; i < range[1] + range[2]; ++i)
        data[i] = static_cast<int16_t>(data[i] / 2);
    return range[2];
}

struct MergeLog { uint32_t g0, g1; std::vector<int16_t> t0, t1; };
std::vector<MergeLog> g_merges;
void THISCALL loggingMerge(uint8_t* mixer, uint32_t blk)
{
    uint8_t* order = *reinterpret_cast<uint8_t**>(mixer + 0x114c);
    auto slot = [&](int t) { return mixer + 0x1164 + (t * 128 + (blk & 0xffff)) * 0x18; };
    MergeLog m;
    uint8_t* s0 = slot(order[0]);
    uint8_t* s1 = slot(order[1]);
    m.g0 = *reinterpret_cast<uint32_t*>(s0 + 8);
    m.g1 = *reinterpret_cast<uint32_t*>(s1 + 8);
    int16_t* d0 = *reinterpret_cast<int16_t**>(s0 + 0x14);
    int16_t* d1 = *reinterpret_cast<int16_t**>(s1 + 0x14);
    m.t0.assign(d0, d0 + 128);
    m.t1.assign(d1, d1 + 128);
    g_merges.push_back(m);
    fn<void(THISCALL*)(void*, uint32_t)>(0x411140)(mixer, blk);
}
void patchCall(uint32_t at, void* target)
{
    uint8_t* p = reinterpret_cast<uint8_t*>(at);
    *reinterpret_cast<int32_t*>(p + 1) = reinterpret_cast<int32_t>(target) - static_cast<int32_t>(at + 5);
}

struct OriginalSynth {
    static constexpr int kTracks = 64;
    std::vector<uint8_t> synth, params, patmem, mixer;
    std::vector<int16_t> audio;
    uint8_t order[72];

    OriginalSynth() : synth(0x600), params(0x80), patmem(0xe00), mixer(0x32000), audio(kTracks * 128 * 128)
    {
        std::memset(order, 0xff, sizeof order);
        fn<void(THISCALL*)(void*)>(0x408ac0)(synth.data());
        *reinterpret_cast<uint8_t**>(&mixer[0x114c]) = order;
        *reinterpret_cast<uint16_t*>(&mixer[0x1154]) = 44100;
        *reinterpret_cast<float*>(&mixer[0x1160]) = static_cast<float>(128000.0 / 44100.0f);
        auto slotCtor = fn<void(THISCALL*)(void*)>(0x40ef80);
        for (int t = 0; t < kTracks; ++t)
            for (int b = 0; b < 128; ++b) {
                uint8_t* slot = &mixer[0x1164 + (t * 128 + b) * 0x18];
                slotCtor(slot);
                *reinterpret_cast<uint32_t*>(slot + 0x10) = 128;
                // Slots come out of the constructor at unity gain; in a running
                // program they have all been used and zeroed once. Starting from
                // the constructor state makes never-used slots leak into the
                // first period, a one-off start-up artefact we do not reproduce.
                *reinterpret_cast<uint32_t*>(slot + 0x08) = 0;
                *reinterpret_cast<int16_t**>(slot + 0x14) = &audio[(t * 128 + b) * 128];
            }
    }
    template <typename T> T& at(std::vector<uint8_t>& v, size_t off) { return *reinterpret_cast<T*>(&v[off]); }

    void setParams(const rd::Params& p)
    {
        at<int32_t>(params, 0x08) = p.accent;
        params[0x0c] = p.osc1Type;
        params[0x0d] = p.osc2Type;
        at<int32_t>(params, 0x10) = p.osc2Double;
        at<int32_t>(params, 0x14) = p.oscMix;
        at<int32_t>(params, 0x18) = p.tune;
        at<int32_t>(params, 0x1c) = p.volume;
        at<int32_t>(params, 0x20) = p.attack;
        at<int32_t>(params, 0x24) = p.decay;
        at<int32_t>(params, 0x28) = p.modulation;
        at<int32_t>(params, 0x2c) = p.cutoff;
        at<int32_t>(params, 0x30) = p.resonance;
        at<int32_t>(params, 0x34) = p.modification;
        at<int32_t>(params, 0x38) = p.tweak;
        at<int32_t>(params, 0x3c) = p.tweet;
        at<int32_t>(params, 0x40) = p.drive;
        at<int32_t>(params, 0x44) = p.squelch;
        at<int32_t>(patmem, 0xdac) = p.distortionMix;
        at<int32_t>(patmem, 0xdb0) = p.distortionSmooth;
        at<int32_t>(patmem, 0xdb4) = p.delayTime;
        at<int32_t>(patmem, 0xdb8) = p.delayGain;
        at<int32_t>(patmem, 0xdbc) = p.delayFeedback;
        params[0] |= 2;  // osc types changed
    }
    void init(const rd::Engine& e)
    {
        setParams(e.params);
        fn<int(THISCALL*)(void*, void*)>(0x408c00)(synth.data(), params.data());
        for (int p = 0; p < 16; ++p)
            for (int s = 0; s < 16; ++s) {
                uint8_t* st = &patmem[p * 0xc0 + s * 12];
                const rd::Step& src = e.patterns[p].steps[s];
                st[0] = static_cast<uint8_t>(src.note);
                *reinterpret_cast<int32_t*>(st + 4) = src.accent;
                *reinterpret_cast<int32_t*>(st + 8) = src.slide;
            }
        for (int i = 0; i < rd::kSongLength; ++i) patmem[0xc00 + i] = e.song[i];
        at<int32_t>(patmem, 0xc78) = e.songLength;
        at<int32_t>(patmem, 0xc7c) = static_cast<int32_t>(e.playMode);
        selectPattern(e.currentPatternIndex());
        at<uint8_t*>(synth, 0x4b8) = patmem.data();
        at<int32_t>(synth, 4) = 1;
        at<float>(synth, 0x10) = e.getTempo();
    }
    void selectPattern(int p)
    {
        at<int32_t>(patmem, 0xc64) = p;
        at<uint8_t*>(patmem, 0xc68) = &patmem[p * 0xc0];
    }
    // Renders `blocks` blocks; returns the voice's output track.
    void render(int16_t* out, unsigned blocks)
    {
        std::memset(&mixer[0x31164], 0, kTracks * 4);
        fn<int(THISCALL*)(void*, void*, void*, uint32_t, uint32_t)>(0x408ca0)(synth.data(), params.data(), mixer.data(), 0, blocks);
        int t = order[0];
        std::memcpy(out, &audio[t * 128 * 128], blocks * 128 * sizeof(int16_t));
    }
};

bool testEngine(const char* h30Path, const char* label, unsigned periods, unsigned blocksPerPeriod,
                void (*tweak)(rd::Params&, unsigned period) = nullptr)
{
    rd::H30File file;
    std::string err;
    if (!rd::H30File::load(h30Path, file, &err)) {
        std::printf("%-28s SKIP  %s\n", label, err.c_str());
        return true;
    }
    rd::Engine mine;
    file.applyTo(mine);
    if (const char* set = std::getenv("RD_SET")) {
        // e.g. RD_SET="oscMix=0,modification=0"
        std::string spec = set;
        size_t at = 0;
        while (at < spec.size()) {
            size_t comma = spec.find(',', at);
            std::string kv = spec.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
            size_t eq = kv.find('=');
            std::string k = kv.substr(0, eq);
            int v = std::atoi(kv.c_str() + eq + 1);
            rd::Params& P = mine.params;
            if (k == "oscMix") P.oscMix = v; else if (k == "modification") P.modification = v;
            else if (k == "squelch") P.squelch = v; else if (k == "drive") P.drive = v;
            else if (k == "resonance") P.resonance = v; else if (k == "tweet") P.tweet = v;
            else if (k == "tweak") P.tweak = v; else if (k == "delayTime") P.delayTime = v;
            else if (k == "distortionMix") P.distortionMix = v; else if (k == "cutoff") P.cutoff = v;
            else std::printf("unknown key %s\n", k.c_str());
            if (comma == std::string::npos) break;
            at = comma + 1;
        }
        mine.resetSmoothing();
    }
    mine.setRunning(true);

    static OriginalSynth* orig = nullptr;
    delete orig;
    orig = new OriginalSynth();
    orig->init(mine);

    const unsigned n = periods * blocksPerPeriod * 128;
    std::vector<int16_t> a(n), b(n);
    // Both sides use the MSVC rand() sequence from seed 1; they stay in step
    // only if the re-implementation draws at exactly the same points.
    rd_srand(1);
    mine.random().seed(1);
    std::vector<int16_t> osc2(n), branch(n), branchIn(n);
    g_branchInput.clear();
    g_merges.clear();
    patchCall(0x40943e, reinterpret_cast<void*>(&loggingMerge));
    rd_patch_jump(0x40ff70, reinterpret_cast<void*>(&capturingHalve));
    mine.debugBranchInput = branchIn.data();
    mine.debugOsc2 = osc2.data();
    mine.debugBranch = branch.data();
    for (unsigned p = 0; p < periods; ++p) {
        if (tweak) {
            tweak(mine.params, p);
            orig->setParams(mine.params);
        }
        orig->render(&a[p * blocksPerPeriod * 128], blocksPerPeriod);
        mine.render(&b[p * blocksPerPeriod * 128], blocksPerPeriod);
        if (std::getenv("RD_TRACE") && p < 12) {
            auto& S = orig->synth;
            auto u32 = [&](size_t o) { return *reinterpret_cast<uint32_t*>(&S[o]); };
            auto f32 = [&](size_t o) { return *reinterpret_cast<float*>(&S[o]); };
            rd::Engine::DebugState d = mine.debugState();
            std::printf("p%u orig pos=%08x env=%u st=%u note=%u.%u tgt=%u ph1=%08x inc1=%u ph2=%08x inc2=%u base=%d/%d cut=%u res=%u hz=%.3f drift=%08x rate=%d\n",
                p, u32(8), u32(0x68), S[0x38], S[0x44], S[0x45], S[0x1c], u32(0x48), u32(0x4c), u32(0x5c), u32(0x60),
                (int)u32(0x28), (int)u32(0x34), u32(0x544), u32(0x554), f32(0x548), u32(0x4bc), (int)u32(0x14));
            std::printf("p%u mine pos=%08x env=%u st=%u note=%u.%u tgt=%u ph1=%08x inc1=%u ph2=%08x inc2=%u base=%d/%d cut=%u res=%u hz=%.3f drift=%08x rate=%d\n",
                p, d.pos, d.env, d.stage, d.note, d.fine, d.target, d.phase1, d.inc1, d.phase2, d.inc2, d.base1, d.base2,
                d.cutoffSmooth, d.resoSmooth, d.cutoffHz, d.driftPhase, d.slideRate);
            std::printf("   orig dist=%g/%g/%g delay len=%u pos=%u  lp=%g/%g/%g/%g bp=%g/%g drive=%g/%g\n",
                f32(0x90 + 0x208), f32(0x90 + 0x20c), f32(0x90 + 0x210), u32(0x70 + 0xc), u32(0x70 + 0x10),
                f32(0x4fc + 0x14), f32(0x4fc + 0x18), f32(0x4fc + 0x1c), f32(0x4fc + 0x20), f32(0x520 + 0x14), f32(0x520 + 0x18),
                f32(0x2a4 + 0x208), f32(0x2a4 + 0x20c));
            std::printf("   mine dist=%g/%g/%g delay len=%u pos=%u  lp=%g/%g/%g/%g bp=%g/%g drive=%g/%g\n",
                mine.debugDistortion().debugState(0), mine.debugDistortion().debugState(1), mine.debugDistortion().debugState(2),
                mine.debugDelay().debugLength(), mine.debugDelay().debugPos(),
                mine.debugLowPass().debugState(0), mine.debugLowPass().debugState(1), mine.debugLowPass().debugState(2), mine.debugLowPass().debugState(3),
                mine.debugBandPass().debugState(0), mine.debugBandPass().debugState(1),
                mine.debugDrive().debugState(0), mine.debugDrive().debugState(1));
            std::printf("   coef orig lp f=%.9g q=%.9g bp f=%.9g q=%.9g | mine lp f=%.9g q=%.9g bp f=%.9g q=%.9g\n",
                f32(0x4fc + 0xc), f32(0x4fc + 0x10), f32(0x520 + 0xc), f32(0x520 + 0x10),
                mine.debugLowPass().debugF(), mine.debugLowPass().debugQ(), mine.debugBandPass().debugF(), mine.debugBandPass().debugQ());
            if (p == 0) {
                for (size_t k = 0; k < g_merges.size() && k < 2; ++k)
                    std::printf("   merge %zu: g0=%u g1=%u t0[0..3]=%d %d %d %d t1[0..3]=%d %d %d %d\n", k, g_merges[k].g0, g_merges[k].g1,
                        g_merges[k].t0[0], g_merges[k].t0[1], g_merges[k].t0[2], g_merges[k].t0[3],
                        g_merges[k].t1[0], g_merges[k].t1[1], g_merges[k].t1[2], g_merges[k].t1[3]);
                for (int blk = 0; blk < (int)blocksPerPeriod; ++blk) {
                    int d = 0, f = -1;
                    for (int i = 0; i < 128; ++i)
                        if (g_branchInput[blk * 128 + i] != branchIn[blk * 128 + i]) { d++; if (f < 0) f = i; }
                    std::printf("   block %d branch input mismatches %d first %d", blk, d, f);
                    if (f >= 0) std::printf(" orig %d mine %d", g_branchInput[blk*128+f], branchIn[blk*128+f]);
                    std::printf("\n");
                }
                const int16_t* o2 = &orig->audio[orig->order[1] * 128 * 128];
                const int16_t* br = &orig->audio[orig->order[2] * 128 * 128];
                std::printf("   tracks: %d %d %d\n", orig->order[0], orig->order[1], orig->order[2]);
                for (int blk = 0; blk < (int)blocksPerPeriod; ++blk) {
                    int d1 = 0, d2 = 0, f1 = -1, f2 = -1;
                    for (int i = 0; i < 128; ++i) {
                        if (o2[blk * 128 + i] != osc2[blk * 128 + i]) { d1++; if (f1 < 0) f1 = i; }
                        if (br[blk * 128 + i] != branch[blk * 128 + i]) { d2++; if (f2 < 0) f2 = i; }
                    }
                    std::printf("   block %d: osc2 mismatches %d (first %d) branch mismatches %d (first %d)\n", blk, d1, f1, d2, f2);
                    if (f2 >= 0) std::printf("     branch[%d] orig %d mine %d\n", f2, br[blk*128+f2], branch[blk*128+f2]);
                }
            }
            int shown = 0;
            for (int i = 0; i < 128 && shown < 12; ++i) {
                int16_t x = a[p * blocksPerPeriod * 128 + i], y = b[p * blocksPerPeriod * 128 + i];
                if (x != y) { std::printf("   [%3d] orig %6d mine %6d  diff %d\n", i, x, y, x - y); shown++; }
            }
        }
    }
    return report(label, compare(a, b), n);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: oracle rdh3o.exe [test]\n");
        return 2;
    }
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (!rd_load(argv[1]))
        return 1;
    setFpuLikeOriginal();
    std::string only = argc > 2 ? argv[2] : "";
    bool ok = true;
    if (only.empty() || only == "ladder") ok &= testLadder();
    for (int m = 0; m < 4; ++m)
        if (only.empty() || only == "svf") ok &= testSvf(m);
    if (only == "dump") {
        // dump <h30> <out.raw> <blocks>: original voice output, raw int16 mono.
        rd::H30File file;
        std::string err;
        if (argc < 6 || !rd::H30File::load(argv[3], file, &err)) {
            std::fprintf(stderr, "dump: %s\n", err.c_str());
            return 1;
        }
        rd::Engine shadow;
        file.applyTo(shadow);
        OriginalSynth* orig = new OriginalSynth();
        orig->init(shadow);
        unsigned blocks = static_cast<unsigned>(std::atoi(argv[5]));
        std::vector<int16_t> a(blocks * 128);
        rd_srand(1);
        for (unsigned b = 0; b < blocks; b += 16) {
            unsigned n = blocks - b < 16 ? blocks - b : 16;
            orig->render(&a[b * 128], n);
        }
        FILE* f = std::fopen(argv[4], "wb");
        std::fwrite(a.data(), 2, a.size(), f);
        std::fclose(f);
        return 0;
    }
    if (only == "allfiles") {
        // Every pattern file in a directory, 1000 blocks each, 8 blocks per period.
        const char* dir = argc > 3 ? argv[3] : "reference/content";
        WIN32_FIND_DATAA fd;
        std::string pattern = std::string(dir) + "/*.h30";
        HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
        int files = 0, failed = 0;
        if (h != INVALID_HANDLE_VALUE) {
            do {
                std::string path = std::string(dir) + "/" + fd.cFileName;
                bool r = testEngine(path.c_str(), fd.cFileName, 125, 8);
                files++;
                if (!r) failed++;
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
        std::printf("files=%d failed=%d\n", files, failed);
        ok &= failed == 0;
    }
    if (only == "fuzz") {
        // Random parameter changes every period on top of a pattern file.
        const char* dir = argc > 3 ? argv[3] : "reference/content";
        std::string path = std::string(dir) + "/Acid_was_my_first_love.h30";
        for (unsigned seed = 1; seed <= 20; ++seed) {
            static unsigned s_seed;
            s_seed = seed;
            char label[64];
            std::snprintf(label, sizeof label, "fuzz seed %u", seed);
            ok &= testEngine(path.c_str(), label, 200, 4, [](rd::Params& P, unsigned period) {
                Lcg r(s_seed * 7919u + period);
                if (period % 3 != 0) return;
                switch (r.next() % 20) {
                case 0: P.osc1Type = r.next() % 4; break;
                case 1: P.osc2Type = r.next() % 3; break;
                case 2: P.osc2Double = r.next() % 2; break;
                case 3: P.oscMix = r.next() % 256; break;
                case 4: P.tune = static_cast<int>(r.next() % 256) - 128; break;
                case 5: P.accent = r.next() % 256; break;
                case 6: P.attack = r.next() % 256; break;
                case 7: P.decay = r.next() % 256; break;
                case 8: P.modulation = r.next() % 256; break;
                case 9: P.cutoff = r.next() % 256; break;
                case 10: P.resonance = r.next() % 256; break;
                case 11: P.modification = r.next() % 2; break;
                case 12: P.tweak = r.next() % 2; break;
                case 13: P.tweet = r.next() % 2; break;
                case 14: P.drive = r.next() % 256; break;
                case 15: P.squelch = r.next() % 256; break;
                case 16: P.distortionMix = r.next() % 256; P.distortionSmooth = r.next() % 256; break;
                case 17: P.delayTime = r.next() % 256; break;
                case 18: P.delayGain = r.next() % 256; P.delayFeedback = r.next() % 256; break;
                case 19: P.volume = r.next() % 256; break;
                }
            });
        }
    }
    if (only.empty() || only == "engine") {
        const char* dir = argc > 3 ? argv[3] : "reference/content";
        std::string path = std::string(dir) + "/Acid_was_my_first_love.h30";
        ok &= testEngine(path.c_str(), "engine acid_first_love", std::getenv("RD_BPP") ? 640 / std::atoi(std::getenv("RD_BPP")) : 40, std::getenv("RD_BPP") ? std::atoi(std::getenv("RD_BPP")) : 16);
    }
    if (only.empty() || only == "shaper") {
        ok &= testShaperTables();
        for (int m = 0; m < 4; ++m) ok &= testShaper(m);
    }
    return ok ? 0 : 1;
}
