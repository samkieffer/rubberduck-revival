#include "rd/EngineV1.h"

#include <cmath>
#include <cstring>

#include "rd/Numeric.h"
#include "rd/Tables.h"

namespace rd {

namespace {

constexpr int kSampleRateV1 = 44100;
// Block duration in ms as stored by the original mixer (0x405c20).
const float kBlockMsV1 = static_cast<float>(64000.0 / kSampleRateV1);
const double kPi = 3.141592653589793;
const double kLn16 = std::log(16.0);
const double kLn32 = std::log(32.0);
const double kLn65536 = std::log(65536.0);

// MSVC __ftol keeping the low 32 bits of the 64-bit result, as the original
// does when it stores a phase increment.
inline uint32_t ftol32(double x)
{
    if (!(std::fabs(x) < 9223372036854775808.0))
        return 0;  // x87 "integer indefinite" 0x8000000000000000
    return static_cast<uint32_t>(static_cast<uint64_t>(static_cast<int64_t>(x)));
}

// Mixer "apply gain" (0x406a60): gain 0x8000 is unity.
void applyGain(int16_t* buf, unsigned n, uint32_t gain)
{
    if (gain == 0x8000)
        return;
    const float g = static_cast<float>(static_cast<double>(gain) * 3.0517578125e-05f);
    const int32_t w = ftol(static_cast<double>(g) * 65536.0f);
    if (static_cast<double>(g) <= 1.0) {
        // 0x4067c0
        for (unsigned i = 0; i < n; ++i)
            buf[i] = static_cast<int16_t>(divPow2(w * buf[i], 16));
    } else {
        // 0x406670: saturates, otherwise rounds down.
        const int32_t hi = ftol(32767.0 / static_cast<double>(g));
        const int32_t lo = ftol(-32768.0 / static_cast<double>(g));
        for (unsigned i = 0; i < n; ++i) {
            const int32_t s = buf[i];
            if (s >= hi)
                buf[i] = 0x7fff;
            else if (s <= lo)
                buf[i] = -0x8000;
            else
                buf[i] = static_cast<int16_t>(static_cast<uint32_t>(w * s) >> 16);
        }
    }
}

// Mixer track merge (0x406910): weighted average of two tracks by gain.
void mergeInto(int16_t* dst, uint32_t dstGain, const int16_t* src, uint32_t srcGain, unsigned n)
{
    if (srcGain == 0)
        return;
    if (dstGain == 0) {
        std::memcpy(dst, src, n * sizeof(int16_t));
        return;
    }
    const double sum = static_cast<double>(static_cast<uint64_t>(dstGain) + srcGain);
    const int32_t wDst = ftol(static_cast<double>(dstGain) * 65536.0f / sum);
    const int32_t wSrc = ftol(static_cast<double>(srcGain) * 65536.0f / sum);
    for (unsigned i = 0; i < n; ++i)
        dst[i] = static_cast<int16_t>((src[i] * wSrc + wDst * dst[i]) >> 16);
}

} // namespace

EngineV1::EngineV1()
{
    // Default patterns of the pattern memory constructor 0x40c640: all rests.
    for (auto& p : patterns)
        for (auto& s : p.steps)
            s = Step{};
    delay.init(0x3fc, kSampleRateV1);
    delay.set(0, 0.0f, 0.0f);
    setTempoKnob(0x36);
    reset();
}

void EngineV1::reset()
{
    paramFlags = 7;
    osc1Kind = 3;
    osc2Kind = 1;
    envStage = 0;
    envPeak = 0;
    voice1.flags = 0;
    voice1.pan = 0;
    voice1.amplitude = 0x100;
    voice1.fine = 0;
    level = 0;
    tuneSmooth = params.tune;
    cutoffSmooth = static_cast<uint32_t>(params.cutoff);
    modSmooth = static_cast<uint32_t>(params.modulation);
    resSmooth = static_cast<uint32_t>(params.resonance);
    lastOsc1 = lastOsc2 = 0xff;
    lastTempoKnob = -1;
}

void EngineV1::setTempoKnob(int knob)
{
    tempo = static_cast<float>(60.0 - static_cast<double>(static_cast<uint32_t>(knob * 190)) * -0.00392156862745098);
}

void EngineV1::selectPattern(int index)
{
    if (index >= 0 && index < kNumPatterns)
        currentPattern = index;
}

void EngineV1::rewind()
{
    step = 0;
    pos = 0;
}

int32_t EngineV1::blockIncrement() const
{
    return ftol(static_cast<double>(kBlockMsV1) * 262144.0f * tempo * 1.6666666666666667e-05);
}

int EngineV1::setNextPosition(uint32_t p)
{
    const uint32_t s = p == 0 ? 0 : (p - 1) >> 16;  // step playing after this block
    const bool crossing = s != syncStep;
    const bool force = forceNext;
    forceNext = false;
    syncStep = s;
    if (force || (p == 0 && crossing)) {
        step = static_cast<int>(s & 15);
        pos = static_cast<int32_t>(p - (s << 16));
        pendingTrigger = pos != 0;  // pos 0 triggers by itself
        return kNewStep | (step == 0 ? kNewBar : 0);
    }
    if (crossing) {
        // Leave the counter just past the end of the previous step so that the
        // sequencer advances (and wraps the bar) exactly as when free running.
        step = static_cast<int>((s - 1) & 15);
        pos = static_cast<int32_t>(p - ((s - 1) << 16));
        return kNewStep | ((s & 15) == 0 ? kNewBar : 0);
    }
    step = static_cast<int>(s & 15);
    pos = static_cast<int32_t>(p - (s << 16));
    if (pos == 0)
        pos = 1;
    return 0;
}

void EngineV1::noteOn(int note, bool accent, bool slide)
{
    live.note = static_cast<int8_t>(note < 0 ? 0 : (note > 120 ? 120 : note));
    live.accent = accent ? 1 : 0;
    live.slide = slide ? 1 : 0;
    liveNote = true;
    // A note-off received earlier in the same block belongs to the previous
    // note; applied after this one, it would release the new note at once.
    liveOff = false;
}

void EngineV1::noteOff()
{
    liveOff = true;
}

const Step& EngineV1::stepData(int pattern, int index) const
{
    return patterns[static_cast<size_t>(pattern)].steps[static_cast<size_t>(index)];
}

void EngineV1::changePattern(int index)
{
    // The original checks index < 15, one past its 14 patterns: Random mode
    // can pick rangeEnd + 1, and index 14 lay over the sequencer state, not
    // reproducible, so it played as rests here. With 16 patterns, index 14 is
    // pattern E, rests too while it is empty.
    if (index < kNumPatterns && index >= 0)
        currentPattern = index;
}

int32_t EngineV1::pitchDistance(uint8_t target) const
{
    return (static_cast<int32_t>(target) << 7) - voice1.fine - (static_cast<int32_t>(voice1.note) << 7);
}

void EngineV1::movePitch(int32_t amount)
{
    // 0x401fa0: note.fine in 8.7 fixed point.
    const uint32_t total = (static_cast<uint32_t>(voice1.note) << 7) + static_cast<uint32_t>(amount)
                           + static_cast<uint32_t>(static_cast<int32_t>(voice1.fine));
    voice1.note = static_cast<uint8_t>(total >> 7);
    voice1.fine = static_cast<int8_t>(total & 0x7f);
}

void EngineV1::stepAt(const Step& s)
{
    const uint8_t c = static_cast<uint8_t>(s.note);
    if (c == 0xff) {
        envStage = 4;
    } else if (c != 0xfe) {
        targetNote = c;
        if (s.slide == 0) {
            voice1.note = c;
            voice1.fine = 0;
            envStage = 1;
        }
    }
    if (s.accent != 0) {
        envStage = 1;
        envPeak = static_cast<uint8_t>((static_cast<uint32_t>(params.accent) >> 1) + 0x80);
    } else {
        envPeak = 0xa0;
    }
    // Glide rate: covers the note distance in 7/8 of a step.
    const int32_t rate = (pitchDistance(targetNote) * 8) / (blocksPerStep * 7);
    slideRate = (rate < 0 ? -rate : rate) + 1;
}

void EngineV1::triggerStep()
{
    stepAt(stepData(currentPattern, step));
}

void EngineV1::sequencer()
{
    if (!running)
        return;
    if (pendingTrigger) {
        pendingTrigger = false;
        triggerStep();
    } else if (pos > 0x10000) {
        pos -= 0x10000;
        if (step < 15) {
            step++;
        } else {
            step = 0;
            if (stopAtEnd) {
                stopAtEnd = false;
                running = false;
                pos = 0;
                return;
            }
            if (playMode == PlayModeV1::Sequence) {
                changePattern(currentPattern < rangeEnd ? currentPattern + 1 : rangeStart);
            } else if (playMode == PlayModeV1::Random) {
                const int32_t r = rng.next();
                changePattern(r * (rangeEnd - rangeStart + 1) / 0x7fff + rangeStart);
            }
        }
        triggerStep();
    } else if (pos == 0) {
        triggerStep();
    }
    pos += inc;
}

void EngineV1::oscillator(uint8_t kind, int32_t freq, Voice& v, int16_t* buf)
{
    // 0x404cd0
    if (v.flags & 2) {
        const float t = pitchTableV1((static_cast<int>(v.note) << 5) + (v.fine * 32) / 128);
        v.increment = ftol32(static_cast<double>(t) * freq / kSampleRateV1 * 32768.0f);
        v.flags &= ~2;
    }
    switch (kind) {
    case 1:  // sine, two cycles per phase turn
        for (unsigned i = 0; i < kBlockSizeV1; ++i, v.phase += v.increment)
            buf[i] = static_cast<int16_t>(ftol(std::sin(static_cast<double>(v.phase) * 2.9258361585343192e-09) * 32767.0));
        break;
    case 2:  // saw, then silence for the second half
        for (unsigned i = 0; i < kBlockSizeV1; ++i, v.phase += v.increment)
            buf[i] = v.phase < 0x80000000u ? static_cast<int16_t>((v.phase >> 15) - 0x8000) : 0;
        break;
    case 3:  // saw
        for (unsigned i = 0; i < kBlockSizeV1; ++i, v.phase += v.increment)
            buf[i] = static_cast<int16_t>(v.phase >> 16);
        break;
    case 4:  // square
        for (unsigned i = 0; i < kBlockSizeV1; ++i, v.phase += v.increment)
            buf[i] = static_cast<int16_t>(((v.phase >> 30) & 2) + 0x7fff);
        break;
    case 5:  // noise (unipolar)
        for (unsigned i = 0; i < kBlockSizeV1; ++i, v.phase += v.increment)
            buf[i] = static_cast<int16_t>(rng.next());
        break;
    case 6:  // triangle, RubDuck's: v2.12's waveform (the original has none)
        for (unsigned i = 0; i < kBlockSizeV1; ++i, v.phase += v.increment)
            buf[i] = v.phase < 0x80000000u ? static_cast<int16_t>((v.phase >> 15) - 0x8000)
                                           : static_cast<int16_t>(((~v.phase) >> 15) - 0x7fff);
        break;
    default:
        break;
    }
}

void EngineV1::Filter::process(int16_t* buf, unsigned n)
{
    // 0x4013f0: two chained state-variable stages, input scaled by 0.13,
    // output the second low-pass (truncated, wrapping to 16 bits).
    for (unsigned i = 0; i < n; ++i) {
        const double L1 = static_cast<double>(f) * b1 + l1;
        const double L2 = static_cast<double>(f) * b2 + l2;
        l1 = static_cast<float>(L1);
        const float l2n = static_cast<float>(L2);
        const double h1 = (static_cast<double>(buf[i]) * 0.13 - L1) - static_cast<double>(damping) * b1;
        const double h2 = (static_cast<double>(l1) - l2n) - static_cast<double>(damping) * l1;
        const double nb2 = h2 * f + l1;
        const double nb1 = static_cast<double>(f) * h1 + b1;
        l2 = l2n;
        b2 = static_cast<float>(nb2);
        b1 = static_cast<float>(nb1);
        buf[i] = static_cast<int16_t>(static_cast<int64_t>(l2));
    }
}

void EngineV1::buildDistortionTable(int curve)
{
    // 0x4011c0: logarithmic transfer curve, 257 points over the 16-bit range.
    const int32_t k = (curve & 0xff) + 1;
    const double norm = std::log(static_cast<double>(k) - -1.0f);
    distTable[128] = 0;
    for (int i = 1; i <= 128; ++i) {
        const double x = std::log(static_cast<double>(i * k) * 0.0078125 - -1.0) / norm;
        distTable[static_cast<size_t>(128 + i)] = ftol(x * 32767.0f);
        distTable[static_cast<size_t>(128 - i)] = ftol(x * -32768.0f);
    }
    distCurve = curve & 0xff;
}

void EngineV1::renderBlock(int16_t* t0)
{
    inc = blockIncrement();
    if (inc < 1)
        inc = 1;
    blocksPerStep = 0x10000 / inc;
    if (blocksPerStep < 1)
        blocksPerStep = 1;

    // Live notes, applied like the original's MIDI input before the sequencer.
    if (liveNote) {
        liveNote = false;
        stepAt(live);
    }
    if (liveOff) {
        liveOff = false;
        if (envStage != 0)
            envStage = 4;
    }
    sequencer();

    if (tuneSmooth != params.tune)
        tuneSmooth += tuneSmooth < params.tune ? 1 : -1;

    // Portamento towards the target note; the decay holds while gliding.
    bool gliding = false;
    const int32_t distance = pitchDistance(targetNote);
    if (distance < 0) {
        gliding = true;
        if (distance < -slideRate) {
            movePitch(-slideRate);
        } else {
            voice1.note = targetNote;
            voice1.fine = 0;
        }
    } else if (distance > 0) {
        gliding = true;
        if (distance <= slideRate) {
            voice1.note = targetNote;
            voice1.fine = 0;
        } else {
            movePitch(slideRate);
        }
    }

    // Base frequency with tuning and pitch drift.
    driftPhase += static_cast<uint32_t>(rng.next()) << 9;
    const int32_t drift = kDriftTableV1[driftPhase >> 23];
    freq1 = ftol(static_cast<double>(drift + ((tuneSmooth + 0x200) << 7)) * 65.41);
    freq2 = params.osc2Double ? static_cast<int32_t>(static_cast<uint32_t>(freq1) * 2) : freq1;
    voice1.flags |= 2;

    switch (envStage) {
    case 1:
        displayNote = voice1.note;
        level /= 3;
        envStage = 2;
        voice1.flags |= 3;  // oscillator start 0x404cb0
        voice1.aux = 0;
        voice1.phase = 0;
        break;
    case 2: {
        const uint32_t peak = static_cast<uint32_t>(envPeak) << 8;
        level += static_cast<uint32_t>((params.attack + 0x19) << 5);
        if (peak < level) {
            level = peak;
            envStage = 3;
        }
        break;
    }
    case 3:
        if (!gliding || level >= 0xe600) {
            const uint32_t dec = static_cast<uint32_t>(params.decay * 3 + 0x19);
            if (dec < level) {
                level -= dec;
            } else {
                level = 0;
                envStage = 4;
            }
        }
        break;
    case 4:
        if (level <= 0x1000) {
            displayNote = 0xff;
            level = 0;
            envStage = 0;
            voice1.flags &= ~1;  // oscillator stop 0x407e30
        } else {
            level -= 0x1000;
        }
        break;
    default:
        break;
    }
    voice1.amplitude = static_cast<uint16_t>(level >> 9);

    const uint32_t keepPhase = voice2.phase;
    voice2 = voice1;
    if (params.osc2Double)
        voice2.phase = keepPhase;
    voice1.amplitude = static_cast<uint16_t>(ftol(
        static_cast<double>(static_cast<uint32_t>((0xff - params.oscMix) * voice1.amplitude)) * 0.00392156862745098));
    voice2.amplitude = static_cast<uint16_t>(ftol(
        static_cast<double>(static_cast<uint32_t>(params.oscMix * voice2.amplitude)) * 0.00392156862745098));

    uint32_t gain = 0;
    if (voice1.flags & 1) {
        oscillator(osc1Kind, freq1, voice1, t0);
        gain = static_cast<uint32_t>(static_cast<int32_t>(static_cast<uint32_t>(voice1.amplitude) << 15) >> 8);
        oscillator(osc2Kind, freq2, voice2, bufB.data());
        const uint32_t gainB =
            static_cast<uint32_t>(static_cast<int32_t>(static_cast<uint32_t>(voice2.amplitude) << 15) >> 8);
        mergeInto(t0, gain, bufB.data(), gainB, kBlockSizeV1);
        gain += gainB;
    }
    applyGain(t0, kBlockSizeV1, gain);

    // Filter: cutoff and resonance follow the envelope (inverse-log curve).
    if (cutoffSmooth != static_cast<uint32_t>(params.cutoff))
        cutoffSmooth += cutoffSmooth < static_cast<uint32_t>(params.cutoff) ? 1 : -1;
    const float cut = static_cast<float>(
        1.0 - (std::log(1021.0f - static_cast<double>(cutoffSmooth << 2)) * 0.5) / kLn32);
    if (modSmooth != static_cast<uint32_t>(params.modulation))
        modSmooth += modSmooth < static_cast<uint32_t>(params.modulation) ? 1 : -1;
    const double m = (std::log(static_cast<double>(modSmooth) - -1.0) * 0.5) / kLn16;
    const double e = 1.0 - std::log(65536.0f - static_cast<double>(level)) / kLn65536;
    const float mf = static_cast<float>(m);
    const float ef = static_cast<float>(e);
    const double a = static_cast<double>(mf) * cut * 3000.0;
    const double b = e * 11000.0f * (1.0 - m);
    cutoffHz = static_cast<float>(((a + b) - static_cast<double>(cut) * -100.0f) - -40.0);
    filter.f = static_cast<float>(2.0 * std::sin(static_cast<double>(cutoffHz) * kPi / filter.rate));
    if (resSmooth != static_cast<uint32_t>(params.resonance))
        resSmooth += resSmooth < static_cast<uint32_t>(params.resonance) ? 1 : -1;
    const float r = static_cast<float>(
        (1.0 - (std::log(256.0f - static_cast<double>(resSmooth)) * 0.5) / kLn16) * ef);
    filter.damping = static_cast<float>(1.0 / (2.0 - static_cast<double>(r) * -400.0f));
    filter.process(t0, kBlockSizeV1);
    for (unsigned i = 0; i < kBlockSizeV1; ++i)
        t0[i] = saturate16(t0[i] * 2);  // 0x406870
}

void EngineV1::render(int16_t* out, unsigned blocks)
{
    if (paramFlags & 4) {
        // Filter initialisation 0x401350 / 0x4013e0.
        paramFlags &= ~4;
        cutoffHz = static_cast<float>(static_cast<double>(kSampleRateV1) * 0.0625f);
        filter.rate = kSampleRateV1 >> 1;
        filter.f = static_cast<float>(2.0 * std::sin(static_cast<double>(cutoffHz) * kPi / filter.rate));
        filter.damping = static_cast<float>(1.0 / (2.0 - 0.0 * -400.0f));
        filter.b1 = filter.l1 = filter.b2 = filter.l2 = 0.0f;
    }
    // Oscillator types (only values the original knows change the waveform,
    // and RubDuck's triangle).
    static const uint8_t kOsc1Kinds[4] = {2, 3, 4, 6};
    static const uint8_t kOsc2Kinds[3] = {1, 3, 5};
    if ((paramFlags & 2) || params.osc1Type != lastOsc1 || params.osc2Type != lastOsc2) {
        paramFlags &= ~2;
        if (params.osc1Type < 4)
            osc1Kind = kOsc1Kinds[params.osc1Type];
        if (params.osc2Type < 3)
            osc2Kind = kOsc2Kinds[params.osc2Type];
        lastOsc1 = params.osc1Type;
        lastOsc2 = params.osc2Type;
    }

    for (unsigned b = 0; b < blocks; ++b)
        renderBlock(out + b * kBlockSizeV1);

    const unsigned n = blocks * kBlockSizeV1;
    if (distCurve != (params.distortionCurve & 0xff))
        buildDistortionTable(params.distortionCurve);
    const int32_t amount = params.distortionAmount & 0xff;
    if (amount != 0) {
        // 0x401290: interpolated table lookup mixed with the dry signal.
        for (unsigned i = 0; i < n; ++i) {
            const int32_t s = out[i];
            const int32_t idx = (s + 0x8000) >> 8;
            const int32_t frac = s + 0x8000 - idx * 0x100;
            const int32_t v = divPow2(distTable[static_cast<size_t>(idx)] * (0x100 - frac)
                                          + distTable[static_cast<size_t>(idx + 1)] * frac, 8);
            out[i] = static_cast<int16_t>(divPow2(v * amount + (0xff - amount) * s, 8));
        }
    }
    delay.set(static_cast<uint32_t>(params.delayTime) << 2,
              static_cast<float>(static_cast<double>(static_cast<uint32_t>(params.delayGain)) * 0.00390625),
              static_cast<float>(static_cast<double>(static_cast<uint32_t>(params.delayFeedback)) * 0.00390625));
    delay.process(out, n);
}

void EngineV1::applyVolume(int16_t* buf, unsigned n, int32_t volume, bool rightChannel)
{
    const uint32_t trackGain = static_cast<uint32_t>(volume) << 8;
    applyGain(buf, n, ((rightChannel ? 0x81u : 0x80u) * trackGain) >> 8);
}

EngineV1::DebugState EngineV1::debugState() const
{
    DebugState d{};
    d.pos = static_cast<uint32_t>(pos);
    d.level = level;
    d.phase1 = voice1.phase;
    d.inc1 = voice1.increment;
    d.phase2 = voice2.phase;
    d.inc2 = voice2.increment;
    d.driftPhase = driftPhase;
    d.freq1 = freq1;
    d.freq2 = freq2;
    d.slideRate = slideRate;
    d.step = step;
    d.pattern = currentPattern;
    d.cutoffSmooth = cutoffSmooth;
    d.modSmooth = modSmooth;
    d.resSmooth = resSmooth;
    d.stage = envStage;
    d.note = voice1.note;
    d.fine = static_cast<uint8_t>(voice1.fine);
    d.target = targetNote;
    d.peak = envPeak;
    d.cutoffHz = cutoffHz;
    d.f = filter.f;
    d.damping = filter.damping;
    return d;
}

} // namespace rd
