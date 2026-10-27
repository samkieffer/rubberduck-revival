#include "rd/Engine.h"

#include <cmath>
#include <cstring>

#include "rd/Mixing.h"
#include "rd/Numeric.h"
#include "rd/Tables.h"

namespace rd {

namespace {

// Duration of one 128-sample block in ms, as stored by the original mixer.
const float kBlockMs = static_cast<float>(128000.0 / static_cast<double>(static_cast<float>(kSampleRate)));
const double kLn256 = std::log(256.0);
const double kLn65536 = std::log(65536.0);

inline float oneMinusLogRatio(uint32_t x, double lnFull)
{
    return static_cast<float>(1.0 - std::log(static_cast<double>(x)) / lnFull);
}

} // namespace

Engine::Engine()
{
    // Original init (0x408c00) followed by the sample-rate set-up done on the
    // first render call.
    drive.configure(0, 0, 0xff);
    delay.init(0x3fc, kSampleRate);
    resetSmoothing();

    cutoffHz = static_cast<float>(static_cast<double>(kSampleRate) * 0.0625f);
    lowPass.configure(StateVariableFilter::DoubleLowPass, kSampleRate / 2, cutoffHz, 0.0f);
    lowPass.reset();
    bandPass.configure(StateVariableFilter::BandPass, kSampleRate / 2, cutoffHz, 0.0f);
    bandPass.reset();
    ladder.setCutoff(0.5f);
    ladder.setResonance(0.0f);
    ladder.reset();
    applyOscTypes();
}

void Engine::resetSmoothing()
{
    tuneSmooth = params.tune;
    cutoffSmooth = static_cast<uint32_t>(params.cutoff);
    modSmooth = static_cast<uint32_t>(params.modulation);
    resoSmooth = static_cast<uint32_t>(params.resonance);
}

void Engine::setTempoKnob(int knob)
{
    tempo = static_cast<float>(static_cast<double>(static_cast<float>(static_cast<uint32_t>(knob * 190))) * 0.00392156886f + 60.0f);
}

void Engine::selectPattern(int index)
{
    if (index >= 0 && index < kNumPatterns)
        currentPattern = index;
}

void Engine::rewind()
{
    pos &= 0xfffff;
    prevPos |= 0xfff00000u;
}

void Engine::setPosition(uint32_t p)
{
    pos = p;
    prevPos = p - 1;
    if ((prevPos >> 16) == (pos >> 16))
        prevPos = pos ^ 0xffff0000u;
}

void Engine::applyOscTypes()
{
    static const int osc1Waves[4] = {2, 6, 3, 4};
    static const int osc2Waves[3] = {1, 3, 5};
    if (params.osc1Type < 4)
        osc1Wave = osc1Waves[params.osc1Type];
    if (params.osc2Type < 3)
        osc2Wave = osc2Waves[params.osc2Type];
    lastOsc1Type = params.osc1Type;
    lastOsc2Type = params.osc2Type;
}

int32_t Engine::pitchDistance(const Voice& v, uint8_t target) const
{
    return static_cast<int32_t>(target) * 128 - (static_cast<int32_t>(v.note) * 128 + v.fine);
}

void Engine::movePitch(Voice& v, int32_t amount)
{
    uint32_t total = static_cast<uint32_t>(static_cast<int32_t>(v.note) * 128 + amount + v.fine);
    v.note = static_cast<uint8_t>(total >> 7);
    v.fine = static_cast<int8_t>(total & 0x7f);
}

void Engine::oscillator(int wave, int32_t base, Voice& v, int16_t* buf)
{
    if (v.flags & 2) {
        int index = v.note * 32 + (v.fine * 32) / 128;
        double inc = static_cast<double>(pitchTable(index)) * static_cast<double>(base) / static_cast<double>(kSampleRate) * 32768.0f;
        v.increment = static_cast<uint32_t>(ftol(inc));
        v.flags &= ~2;
    }
    uint32_t phase = v.phase;
    const uint32_t inc = v.increment;
    switch (wave) {
    case 1:  // sine, two cycles per phase turn
        for (unsigned i = 0; i < kBlockSize; ++i, phase += inc)
            buf[i] = static_cast<int16_t>(ftol(std::sin(static_cast<double>(phase) * 2.9258361585343192e-09) * 32767.0));
        break;
    case 2:  // triangle
        for (unsigned i = 0; i < kBlockSize; ++i, phase += inc)
            buf[i] = phase < 0x80000000u ? static_cast<int16_t>((phase >> 15) - 0x8000)
                                         : static_cast<int16_t>(((~phase) >> 15) - 0x7fff);
        break;
    case 3:  // saw
        for (unsigned i = 0; i < kBlockSize; ++i, phase += inc)
            buf[i] = static_cast<int16_t>(phase >> 16);
        break;
    case 4:  // square
        for (unsigned i = 0; i < kBlockSize; ++i, phase += inc)
            buf[i] = (phase & 0x80000000u) ? -0x7fff : 0x7fff;
        break;
    case 5:  // noise (rand() is 0..32767, so it is unipolar)
        for (unsigned i = 0; i < kBlockSize; ++i, phase += inc)
            buf[i] = static_cast<int16_t>(rng.next());
        break;
    case 6:  // saw over the first half of the cycle, silence over the second
        for (unsigned i = 0; i < kBlockSize; ++i, phase += inc)
            buf[i] = phase < 0x80000000u ? static_cast<int16_t>((phase >> 15) - 0x8000) : 0;
        break;
    default:
        std::memset(buf, 0, kBlockSize * sizeof(int16_t));
        break;
    }
    v.phase = phase;
}

void Engine::render(int16_t* out, unsigned blocks)
{
    for (unsigned b = 0; b < blocks; ++b, ++debugBlock)
        renderBlock(out + b * kBlockSize);

    const unsigned n = blocks * kBlockSize;
    distortion.configure(3, static_cast<uint8_t>(params.distortionGain), static_cast<uint8_t>(params.distortionDrive));
    distortion.process(out, n, nullptr);
    delay.set(static_cast<uint32_t>(params.delayTime) << 2,
              static_cast<float>(static_cast<double>(params.delayGain) * 0.00390625),
              static_cast<float>(static_cast<double>(params.delayFeedback) * 0.00390625));
    delay.process(out, n);
}

uint32_t Engine::blockIncrement() const
{
    return static_cast<uint32_t>(ftol(static_cast<double>(kBlockMs) * 262144.0f * tempo * 1.66666669e-05f));
}

void Engine::triggerStep(const Step& s, uint32_t inc)
{
    if (s.note == -1) {
        envStage = 4;
    } else if (s.note != -2) {
        targetNote = static_cast<uint8_t>(s.note);
        if (s.slide == 0) {
            voice1.note = static_cast<uint8_t>(s.note);
            voice1.fine = 0;
            envStage = 1;
        }
    }
    if (s.accent == 0) {
        envPeak = 0xa0;
    } else {
        envPeak = static_cast<uint8_t>((params.accent >> 1) - 0x80);
        envStage = 1;
    }
    // Glide rate: covers the note distance within one step.
    int32_t rate = (pitchDistance(voice1, targetNote) << 8) / static_cast<int32_t>(0x10000u / (inc ? inc : 1));
    if (rate < 0)
        rate = -rate;
    slideRate = rate + 1;
}

void Engine::noteOn(int note, bool accent, bool slide)
{
    Step s;
    s.note = static_cast<int8_t>(note < 0 ? 0 : (note > 120 ? 120 : note));
    s.accent = accent ? 1 : 0;
    s.slide = slide ? 1 : 0;
    triggerStep(s, blockIncrement());
}

void Engine::noteOff()
{
    if (envStage != 0)
        envStage = 4;
}

void Engine::renderBlock(int16_t* t0)
{
    if (params.osc1Type != lastOsc1Type || params.osc2Type != lastOsc2Type)
        applyOscTypes();

    // Sequencer: 2^16 per step, advanced once per block.
    const uint32_t inc = blockIncrement();
    if (running) {
        if ((pos >> 16) != (prevPos >> 16)) {
            step = static_cast<int>((pos >> 16) & 0xf);
            const uint32_t bar = pos >> 20;
            if (bar != (prevPos >> 20) && playMode != PlayMode::Single) {
                if (playMode == PlayMode::Sequence)
                    songPos = static_cast<int>(bar % static_cast<uint32_t>(songLength));
                else
                    songPos = (rng.next() * songLength) / 0x7fff;
                const uint8_t p = song[static_cast<size_t>(songPos) % kSongLength];
                if (p < kNumPatterns)
                    currentPattern = p;
            }
            triggerStep(patterns[static_cast<size_t>(currentPattern)].steps[static_cast<size_t>(step)], inc);
        }
        prevPos = pos;
        pos += inc;
    }

    if (tuneSmooth != params.tune)
        tuneSmooth += tuneSmooth < params.tune ? 1 : -1;

    // Portamento towards the target note; the envelope holds while gliding.
    slideAcc = static_cast<uint32_t>(slideRate) + (slideAcc & 0xff);
    const int32_t glideStep = static_cast<int32_t>(slideAcc >> 8);
    const int32_t distance = pitchDistance(voice1, targetNote);
    bool gliding = false;
    if (distance < 0) {
        gliding = true;
        if (distance < -glideStep) {
            movePitch(voice1, -glideStep);
        } else {
            voice1.note = targetNote;
            voice1.fine = 0;
        }
    } else if (distance > 0) {
        gliding = true;
        if (distance <= glideStep) {
            voice1.note = targetNote;
            voice1.fine = 0;
        } else {
            movePitch(voice1, glideStep);
        }
    }

    // Oscillator base increment with tuning and pitch drift.
    driftPhase += static_cast<uint32_t>(rng.next()) << 9;
    const int32_t drift = kDriftTable[driftPhase >> 23];
    const int32_t base = ftol(static_cast<double>(((tuneSmooth + 0x200) << 7) + drift) * 65.41);
    osc1Base = base;
    osc2Base = params.osc2Double ? base * 2 : base;
    voice1.flags |= 2;

    switch (envStage) {
    case 1:
        currentNote = voice1.note;
        env /= 3;
        envStage = 2;
        voice1.flags |= 3;
        voice1.aux = 0;
        voice1.phase = 0;
        break;
    case 2: {
        const uint32_t peak = static_cast<uint32_t>(envPeak) * 0x100;
        env += static_cast<uint32_t>((params.attack + 0x19) * 0x40);
        if (peak < env) {
            env = peak;
            envStage = 3;
        }
        break;
    }
    case 3:
        if (!gliding || env > 0xefff) {
            const uint32_t dec = static_cast<uint32_t>(params.decay * 6 + 0x28);
            if (dec < env) {
                env -= dec;
            } else {
                env = 0;
                envStage = 4;
            }
        }
        break;
    case 4:
        if (env < 0x1001) {
            currentNote = 0xff;
            env = 0;
            envStage = 0;
            voice1.flags &= ~1;
        } else {
            env -= 0x1000;
        }
        break;
    }
    voice1.amplitude = static_cast<uint16_t>(params.modification ? env >> 10 : env >> 8);

    const uint32_t keepPhase = voice2.phase;
    voice2 = voice1;
    if (params.osc2Double)
        voice2.phase = keepPhase;
    voice1.amplitude = static_cast<uint16_t>(
        ftol(static_cast<double>((0xff - params.oscMix) * voice1.amplitude) * 0.00392156886f));
    voice2.amplitude = static_cast<uint16_t>(
        ftol(static_cast<double>(voice2.amplitude * params.oscMix) * 0.00392156886f));

    int16_t* t1 = bufB.data();
    uint32_t gain0 = 0, gain1 = 0;
    if (voice1.flags & 1) {
        oscillator(osc1Wave, osc1Base, voice1, t0);
        gain0 = static_cast<uint32_t>(voice1.amplitude) << 7;
        oscillator(osc2Wave, osc2Base, voice2, t1);
        if (debugOsc2)
            std::memcpy(debugOsc2 + debugBlock * kBlockSize, t1, kBlockSize * sizeof(int16_t));
        uint32_t gainB = static_cast<uint32_t>(voice2.amplitude) << 7;
        mix::mixInto(t0, gain0, t1, gainB, kBlockSize);
    }
    mix::applyGain(t0, kBlockSize, gain0);

    // Cutoff: envelope shaped by Modulation, scaled by Cut Off (60..3360 Hz).
    cutoffSmooth = (cutoffSmooth * 15 + static_cast<uint32_t>(params.cutoff)) >> 4;
    const float cut = oneMinusLogRatio(0x100 - cutoffSmooth, kLn256);
    if (modSmooth != static_cast<uint32_t>(params.modulation))
        modSmooth += modSmooth < static_cast<uint32_t>(params.modulation) ? 1 : static_cast<uint32_t>(-1);
    const float upper = oneMinusLogRatio(0x10000 - env, kLn65536);
    const double envLinear = static_cast<double>(env) * 1.52587891e-05f;
    const float lower = static_cast<float>(std::log(static_cast<double>(env + 1)) / kLn65536);
    const double m = static_cast<double>(params.modulation) * 0.0078125;
    double shape;
    if (m <= 1.0) {
        shape = (1.0 - m) * lower + m * envLinear;
    } else {
        const double m1 = m - 1.0f;
        shape = m1 * upper + envLinear * (1.0 - m1);
    }
    cutoffHz = static_cast<float>((static_cast<double>(cut) * 3200.0f + 100.0f) * shape + 60.0);
    const float shapeF = static_cast<float>(shape);
    lowPass.setFrequency(cutoffHz);
    bandPass.setFrequency(params.tweet ? static_cast<float>(static_cast<double>(cutoffHz) + cutoffHz) : cutoffHz);
    resoSmooth = (resoSmooth * 3 + static_cast<uint32_t>(params.resonance)) >> 2;

    if (!params.modification) {
        float x = static_cast<float>((static_cast<double>(cutoffSmooth) + 10.0f) * 0.00392156886f * shapeF);
        if (!(x <= 1.0f))
            x = 1.0f;
        ladder.setCutoff(x);
        ladder.setResonance(static_cast<float>(static_cast<double>(resoSmooth) * 0.00392156886f));
        ladder.process(t0, kBlockSize);
        gain0 = 0x8000;
    } else {
        // Second branch: half level, resonant band-pass (Squelch), then Drive.
        std::memcpy(t1, t0, kBlockSize * sizeof(int16_t));
        if (debugBranchInput)
            std::memcpy(debugBranchInput + debugBlock * kBlockSize, t1, kBlockSize * sizeof(int16_t));
        mix::halve(t1, kBlockSize);
        bandPass.setDamping(static_cast<float>(static_cast<double>(params.squelch) * upper * 0.00234375f));
        bandPass.process(t1, kBlockSize);
        drive.configure(params.tweak ? 1 : 0, static_cast<uint8_t>(static_cast<uint32_t>(params.drive) >> 1), 0xff);
        drive.process(t1, kBlockSize, nullptr);
        gain1 = resoSmooth << 15 >> 6;
        if (debugBranch)
            std::memcpy(debugBranch + debugBlock * kBlockSize, t1, kBlockSize * sizeof(int16_t));
        lowPass.setDamping(0.0f);
        lowPass.process(t0, kBlockSize);
        gain0 = (0x850000u - (static_cast<uint32_t>(params.resonance) << 15)) >> 5;
    }
    mix::mixInto(t0, gain0, t1, gain1, kBlockSize);
    mix::applyGain(t0, kBlockSize, gain0);
}

} // namespace rd
