// Oracle tests for the v1.02 engine: run the voice of the original
// RUBDUCK.EXE (init 0x40c870, render 0x40c910) next to rd::EngineV1 and
// compare the output track sample by sample.
//
// Build (see tools/oracle/build.sh) and run under Wine:
//   wine oracle_v1.exe path/to/RUBDUCK.EXE [test] [args]
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

extern "C" {
#include "rdimage.h"
}

#include "rd/EngineV1.h"
#include "rd/H30File.h"

#define THISCALL __attribute__((thiscall))

namespace {

template <typename T> T fn(uint32_t addr) { return reinterpret_cast<T>(addr); }

void setFpuLikeOriginal()
{
    // _controlfp(_PC_53, _MCW_PC) at start-up: 53-bit precision, round to nearest.
    unsigned short cw = 0x027f;
    __asm__ volatile("fldcw %0" : : "m"(cw));
}

struct Lcg {
    uint32_t s;
    explicit Lcg(uint32_t seed) : s(seed) {}
    uint32_t next() { s = s * 1664525u + 1013904223u; return s; }
};

struct Result {
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
    return r;
}

bool report(const char* name, const Result& r, size_t n)
{
    std::printf("%-34s %s  samples=%zu mismatches=%u maxdiff=%d first=%ld\n", name,
                r.mismatches ? "FAIL" : "OK  ", n, r.mismatches, r.maxDiff, r.firstIndex);
    return r.mismatches == 0;
}

// The original voice with its parameter struct, pattern memory and a mixer
// laid out as the original's (0x405a10): 32 tracks of 256 slots of 64
// samples, each track one contiguous buffer.
struct OriginalSynthV1 {
    static constexpr int kTracks = 32;
    std::vector<uint8_t> synth, params, patmem, mixer;
    std::vector<int16_t> audio;
    int16_t accL[64] = {}, accR[64] = {};

    OriginalSynthV1() : synth(0x200), params(0x80), patmem(0xc00), mixer(0x31000), audio(kTracks * 256 * 64)
    {
        fn<void*(THISCALL*)(void*)>(0x40c790)(synth.data());
        fn<void*(THISCALL*)(void*)>(0x40c640)(patmem.data());
        for (int t = 0; t < kTracks; ++t)
            for (int b = 0; b < 256; ++b) {
                uint8_t* slot = &mixer[0x50 + (t * 256 + b) * 0x18];
                slot[4] = 2;  // 16-bit
                at<uint32_t>(slot, 0x08) = 0;
                at<uint16_t>(slot, 0x0c) = 44100;
                at<uint32_t>(slot, 0x10) = 64;
                at<int16_t*>(slot, 0x14) = &audio[(t * 256 + b) * 64];
            }
        for (size_t acc : {size_t{0x300d0}, size_t{0x300e8}}) {
            // Output accumulators (left, right) of the original mixer.
            fn<void(THISCALL*)(void*)>(0x406070)(&mixer[acc]);
            at<uint32_t>(&mixer[acc], 0x10) = 64;
            at<int16_t*>(&mixer[acc], 0x14) = acc == 0x300d0 ? accL : accR;
        }
        fn<void(THISCALL*)(void*, uint32_t)>(0x405c20)(mixer.data(), 44100);  // rate, block ms
        fn<void(THISCALL*)(void*)>(0x406020)(mixer.data());                   // reset
        at<uint8_t*>(synth.data(), 0x98) = patmem.data();
    }
    template <typename T> static T& at(uint8_t* base, size_t off) { return *reinterpret_cast<T*>(base + off); }
    template <typename T> T& at(std::vector<uint8_t>& v, size_t off) { return *reinterpret_cast<T*>(&v[off]); }

    void setParams(const rd::EngineV1& e, bool oscOrTempoChanged)
    {
        const rd::ParamsV1& p = e.params;
        at<int32_t>(params, 0x08) = p.accent;
        params[0x0c] = p.osc1Type;
        params[0x0d] = p.osc2Type;
        at<int32_t>(params, 0x10) = p.osc2Double;
        at<int32_t>(params, 0x14) = p.tune;
        at<int32_t>(params, 0x18) = p.volume;
        at<int32_t>(params, 0x1c) = p.oscMix;
        at<int32_t>(params, 0x20) = p.attack;
        at<int32_t>(params, 0x24) = p.decay;
        at<int32_t>(params, 0x28) = p.modulation;
        at<int32_t>(params, 0x2c) = p.cutoff;
        at<int32_t>(params, 0x30) = p.resonance;
        at<int32_t>(patmem, 0xb14) = p.distortionAmount;
        at<int32_t>(patmem, 0xb18) = p.distortionCurve;
        at<int32_t>(patmem, 0xb1c) = p.delayTime;
        at<int32_t>(patmem, 0xb20) = p.delayGain;
        at<int32_t>(patmem, 0xb24) = p.delayFeedback;
        if (oscOrTempoChanged)
            params[0] |= 2;
    }
    void init(const rd::EngineV1& e, int tempoKnob)
    {
        at<int32_t>(params, 0x34) = tempoKnob;
        setParams(e, true);
        fn<int(THISCALL*)(void*, void*)>(0x40c870)(synth.data(), params.data());
        for (int p = 0; p < rd::kNumPatternsV1; ++p)
            for (int s = 0; s < 16; ++s) {
                uint8_t* st = &patmem[p * 0xc0 + s * 12];
                const rd::Step& src = e.patterns[p].steps[s];
                st[0] = static_cast<uint8_t>(src.note);
                at<int32_t>(st, 4) = src.accent;
                at<int32_t>(st, 8) = src.slide;
            }
        at<int32_t>(patmem, 0xa8c) = e.rangeStart;
        at<int32_t>(patmem, 0xa90) = e.rangeEnd;
        at<int32_t>(patmem, 0xa94) = static_cast<int32_t>(e.playMode);
        selectPattern(e.currentPatternIndex());
        at<int32_t>(synth, 4) = 1;  // running
    }
    void selectPattern(int p)
    {
        at<int32_t>(patmem, 0xa80) = p;
        at<uint8_t*>(patmem, 0xa84) = &patmem[p * 0xc0];
    }
    // Renders `blocks` blocks (one audio period); returns the voice track.
    void render(int16_t* out, unsigned blocks)
    {
        fn<void(THISCALL*)(void*)>(0x406020)(mixer.data());
        fn<int(THISCALL*)(void*, void*, void*, uint32_t, uint32_t)>(0x40c910)(synth.data(), params.data(), mixer.data(), 0,
                                                                              blocks);
        const int t = at<uint8_t*>(mixer, 8)[0];
        std::memcpy(out, &audio[t * 256 * 64], blocks * 64 * sizeof(int16_t));
    }
    // Original output stage 0x405df0 (16-bit stereo) over the last period.
    void output(int16_t* stereo, unsigned blocks)
    {
        fn<void(THISCALL*)(void*, uint32_t, void*, uint32_t, uint32_t, uint32_t)>(0x405df0)(mixer.data(), blocks, stereo, 2, 1, 1);
    }
};

void traceBlock(unsigned p, OriginalSynthV1& o, const rd::EngineV1& mine)
{
    auto& S = o.synth;
    auto u32 = [&](size_t off) { return *reinterpret_cast<uint32_t*>(&S[off]); };
    auto i32 = [&](size_t off) { return *reinterpret_cast<int32_t*>(&S[off]); };
    auto f32 = [&](size_t off) { return *reinterpret_cast<float*>(&S[off]); };
    const auto pm = o.patmem.data();
    rd::EngineV1::DebugState d = mine.debugState();
    std::printf("p%u orig pos=%08x lvl=%u st=%u pk=%u note=%u.%u tgt=%u ph1=%08x inc1=%u ph2=%08x inc2=%u f=%d/%d rate=%d step=%d pat=%d cut=%u mod=%u res=%u hz=%.9g f=%.9g q=%.9g drift=%08x\n",
                p, u32(0xc), u32(0x6c), S[0x3c], S[0x3e], S[0x48], S[0x49], S[0x20], u32(0x4c), u32(0x50), u32(0x60), u32(0x64),
                i32(0x2c), i32(0x38), i32(0x1c), *reinterpret_cast<int32_t*>(pm + 0xa88), *reinterpret_cast<int32_t*>(pm + 0xa80),
                u32(0xc0), u32(0xcc), u32(0xd0), f32(0xc4), f32(0xa4), f32(0xa8), u32(0x9c));
    std::printf("p%u mine pos=%08x lvl=%u st=%u pk=%u note=%u.%u tgt=%u ph1=%08x inc1=%u ph2=%08x inc2=%u f=%d/%d rate=%d step=%d pat=%d cut=%u mod=%u res=%u hz=%.9g f=%.9g q=%.9g drift=%08x\n",
                p, d.pos, d.level, d.stage, d.peak, d.note, d.fine, d.target, d.phase1, d.inc1, d.phase2, d.inc2, d.freq1, d.freq2,
                d.slideRate, d.step, d.pattern, d.cutoffSmooth, d.modSmooth, d.resSmooth, d.cutoffHz, d.f, d.damping, d.driftPhase);
}

// Parameter automation between periods; may set `tempoKnob` (0..255).
typedef void (*Tweak)(rd::EngineV1&, unsigned period, int& tempoKnob);

bool testEngine(const rd::H30File& file, const char* label, unsigned periods, unsigned blocksPerPeriod, Tweak tweak = nullptr,
                rd::PlayModeV1 mode = rd::PlayModeV1::Single, int forceRangeEnd = -1)
{
    rd::EngineV1 mine;
    file.applyTo(mine);
    if (file.version >= 3 || !file.hasParams)
        mine.playMode = mode;
    if (forceRangeEnd >= 0) {
        mine.playMode = mode;
        mine.rangeStart = 0;
        mine.rangeEnd = forceRangeEnd;
    }
    mine.setRunning(true);

    static OriginalSynthV1* orig = nullptr;
    delete orig;
    orig = new OriginalSynthV1();
    orig->init(mine, file.hasParams ? file.tempoKnob : 0x36);
    if (!file.hasParams)
        mine.setTempoKnob(0x36);

    const unsigned n = periods * blocksPerPeriod * 64;
    std::vector<int16_t> a(n), b(n);
    rd_srand(1);
    mine.random().seed(1);
    const bool trace = std::getenv("RD_TRACE") != nullptr;
    for (unsigned p = 0; p < periods; ++p) {
        if (tweak) {
            int knob = -1;
            const uint8_t o1 = mine.params.osc1Type, o2 = mine.params.osc2Type;
            tweak(mine, p, knob);
            bool changed = o1 != mine.params.osc1Type || o2 != mine.params.osc2Type;
            if (knob >= 0) {
                *reinterpret_cast<int32_t*>(&orig->params[0x34]) = knob;
                mine.setTempoKnob(knob);
                changed = true;
            }
            orig->setParams(mine, changed);
        }
        orig->render(&a[p * blocksPerPeriod * 64], blocksPerPeriod);
        mine.render(&b[p * blocksPerPeriod * 64], blocksPerPeriod);
        if (trace && p < 40) {
            traceBlock(p, *orig, mine);
            int shown = 0;
            for (unsigned i = 0; i < blocksPerPeriod * 64 && shown < 6; ++i) {
                int16_t x = a[p * blocksPerPeriod * 64 + i], y = b[p * blocksPerPeriod * 64 + i];
                if (x != y) { std::printf("   [%3u] orig %6d mine %6d\n", i, x, y); shown++; }
            }
        }
    }
    return report(label, compare(a, b), n);
}

bool loadFile(const std::string& path, rd::H30File& f)
{
    std::string err;
    if (!rd::H30File::load(path, f, &err)) {
        std::printf("%-34s SKIP  %s\n", path.c_str(), err.c_str());
        return false;
    }
    return true;
}

std::vector<std::string> listFiles(const char* dir)
{
    std::vector<std::string> out;
    WIN32_FIND_DATAA fd;
    std::string pattern = std::string(dir) + "/*.h30";
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do out.push_back(fd.cFileName);
        while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    return out;
}

bool testOutputStage()
{
    rd::H30File f;
    if (!loadFile("reference/content/Plastic.H30", f))
        return true;
    unsigned bad = 0, total = 0;
    for (int volume = 0; volume < 256; volume += 3) {
        rd::EngineV1 mine;
        f.applyTo(mine);
        mine.params.volume = volume == 255 ? 255 : volume;
        OriginalSynthV1 orig;
        orig.init(mine, f.tempoKnob);
        std::vector<int16_t> track(16 * 64), stereo(16 * 64 * 2);
        rd_srand(1);
        for (int period = 0; period < 20; ++period) {
            orig.render(track.data(), 16);
            orig.output(stereo.data(), 16);
            std::vector<int16_t> right = track;
            rd::EngineV1::applyVolume(track.data(), 16 * 64, mine.params.volume);
            rd::EngineV1::applyVolume(right.data(), 16 * 64, mine.params.volume, true);
            for (unsigned i = 0; i < 16 * 64; ++i, ++total)
                if (track[i] != stereo[2 * i] || right[i] != stereo[2 * i + 1]) bad++;
        }
    }
    std::printf("%-34s %s  samples=%u mismatches=%u\n", "output stage (left, right)", bad ? "FAIL" : "OK  ", total, bad);
    return bad == 0;
}

bool testDistortionTables()
{
    // 0x4011c0 builds its curve into the global table at 0x4199c8.
    alignas(16) uint8_t obj[16] = {};
    rd::EngineV1 e;
    unsigned bad = 0;
    for (int curve = 0; curve < 256; ++curve) {
        fn<void(THISCALL*)(void*, uint32_t, uint32_t)>(0x4011c0)(obj, 0x80, static_cast<uint32_t>(curve));
        const int32_t* table = reinterpret_cast<const int32_t*>(0x4199c8);
        const auto& mine = e.debugDistortionTable(curve);
        if (std::memcmp(table, mine.data(), 257 * sizeof(int32_t)) != 0)
            bad++;
    }
    std::printf("%-34s %s  curves=256 mismatches=%u\n", "distortion tables", bad ? "FAIL" : "OK  ", bad);
    return bad == 0;
}

unsigned g_seed;

void fuzzTweak(rd::EngineV1& e, unsigned period, int& knob)
{
    Lcg r(g_seed * 7919u + period);
    if (period % 2 != 0) return;
    rd::ParamsV1& P = e.params;
    switch (r.next() % 19) {
    case 0: P.osc1Type = static_cast<uint8_t>(r.next() % 3); break;
    case 1: P.osc2Type = static_cast<uint8_t>(r.next() % 3); break;
    case 2: P.osc2Double = r.next() % 2; break;
    case 3: P.oscMix = static_cast<int32_t>(r.next() % 256); break;
    case 4: P.tune = static_cast<int32_t>(r.next() % 256) - 128; break;
    case 5: P.accent = static_cast<int32_t>(r.next() % 256); break;
    case 6: P.attack = static_cast<int32_t>(r.next() % 256); break;
    case 7: P.decay = static_cast<int32_t>(r.next() % 256); break;
    case 8: P.modulation = static_cast<int32_t>(r.next() % 256); break;
    case 9: case 10: P.cutoff = static_cast<int32_t>(r.next() % 256); break;
    case 11: case 12: P.resonance = static_cast<int32_t>(r.next() % 256); break;
    case 13: P.distortionAmount = static_cast<int32_t>(r.next() % 256); break;
    case 14: P.distortionCurve = static_cast<int32_t>(r.next() % 256); break;
    case 15: P.delayTime = static_cast<int32_t>(r.next() % 256); break;
    case 16: P.delayGain = static_cast<int32_t>(r.next() % 256); P.delayFeedback = static_cast<int32_t>(r.next() % 256); break;
    case 17: P.volume = static_cast<int32_t>(r.next() % 256); break;
    case 18: knob = static_cast<int>(r.next() % 256); break;
    }
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: oracle_v1 RUBDUCK.EXE [test] [args]\n");
        return 2;
    }
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (!rd_load_v1(argv[1]))
        return 1;
    setFpuLikeOriginal();
    std::string only = argc > 2 ? argv[2] : "";
    const char* dir = argc > 3 ? argv[3] : "reference/content";
    bool ok = true;

    if (only.empty() || only == "engine") {
        rd::H30File f;
        if (loadFile(std::string(dir) + "/80'$$$.h30", f))
            ok &= testEngine(f, "engine 80'$$$", 200, 8);
    }
    if (only == "allfiles") {
        // allfiles <dir> [blocks] [blocks per period]
        const unsigned bpp = argc > 5 ? static_cast<unsigned>(std::atoi(argv[5])) : 8;
        const unsigned periods = (argc > 4 ? static_cast<unsigned>(std::atoi(argv[4])) : 2000) / bpp;
        int files = 0, failed = 0;
        for (const auto& name : listFiles(dir)) {
            rd::H30File f;
            if (!loadFile(std::string(dir) + "/" + name, f))
                continue;
            files++;
            if (!testEngine(f, name.c_str(), periods, bpp))
                failed++;
        }
        std::printf("files=%d failed=%d\n", files, failed);
        ok &= failed == 0;
    }
    if (only.empty() || only == "output")
        ok &= testOutputStage();
    if (only.empty() || only == "distortion")
        ok &= testDistortionTables();
    if (only.empty() || only == "fuzz") {
        // Random automation on top of pattern files, in all play modes and
        // with several audio period sizes.
        const char* files[] = {"80'$$$.h30", "Plastic.H30", "TRANCE.H30", "Sadness.h30", "ZAP.H30"};
        const unsigned bpp[] = {1, 3, 8, 32};
        for (unsigned seed = 1; seed <= 24; ++seed) {
            rd::H30File f;
            if (!loadFile(std::string(dir) + "/" + files[seed % 5], f))
                continue;
            g_seed = seed;
            char label[64];
            const unsigned b = bpp[seed % 4];
            const auto mode = static_cast<rd::PlayModeV1>(seed % 3);
            std::snprintf(label, sizeof label, "fuzz seed %u (bpp %u, mode %d)", seed, b, static_cast<int>(mode));
            ok &= testEngine(f, label, 3200 / b, b, fuzzTweak, mode, mode == rd::PlayModeV1::Single ? -1 : static_cast<int>(seed % 14));
        }
    }
    if (only == "hashes") {
        // hashes <dir>: FNV-1a 64 of the original voice track (6000 blocks,
        // 32 per period) for every file, in tests/*hashes.txt format.
        for (const auto& name : listFiles(dir)) {
            rd::H30File f;
            if (!loadFile(std::string(dir) + "/" + name, f))
                continue;
            rd::EngineV1 shadow;
            f.applyTo(shadow);
            OriginalSynthV1* orig = new OriginalSynthV1();
            orig->init(shadow, f.hasParams ? f.tempoKnob : 0x36);
            std::vector<int16_t> a(6000 * 64);
            rd_srand(1);
            for (unsigned b = 0; b < 6000; b += 32)
                orig->render(&a[b * 64], 6000 - b < 32 ? 6000 - b : 32);
            delete orig;
            uint64_t h = 0xcbf29ce484222325ull;
            for (int16_t v : a)
                for (int i = 0; i < 2; ++i) {
                    h ^= (static_cast<uint16_t>(v) >> (8 * i)) & 0xff;
                    h *= 0x100000001b3ull;
                }
            std::printf("%016llx  %s|v1\n", static_cast<unsigned long long>(h), name.c_str());
        }
        return 0;
    }
    if (only == "dump") {
        // dump <h30> <out.raw> <blocks>: original voice track, raw int16 mono.
        rd::H30File f;
        if (argc < 6 || !loadFile(argv[3], f))
            return 1;
        rd::EngineV1 shadow;
        f.applyTo(shadow);
        OriginalSynthV1* orig = new OriginalSynthV1();
        orig->init(shadow, f.hasParams ? f.tempoKnob : 0x36);
        unsigned blocks = static_cast<unsigned>(std::atoi(argv[5]));
        std::vector<int16_t> a(blocks * 64);
        rd_srand(1);
        for (unsigned b = 0; b < blocks; b += 32) {
            unsigned n = blocks - b < 32 ? blocks - b : 32;
            orig->render(&a[b * 64], n);
        }
        FILE* out = std::fopen(argv[4], "wb");
        std::fwrite(a.data(), 2, a.size(), out);
        std::fclose(out);
        return 0;
    }
    return ok ? 0 : 1;
}
