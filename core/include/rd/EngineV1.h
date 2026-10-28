#pragma once

#include <array>
#include <cstdint>

#include "rd/Delay.h"
#include "rd/Engine.h"
#include "rd/Random.h"

namespace rd {

// Engine of Rubber Duck H3O+ v1.02 (RUBDUCK.EXE, June 1997): a different
// voice from v2.12, processing 64-sample blocks at 44.1 kHz.
constexpr unsigned kBlockSizeV1 = 64;
constexpr int kNumPatternsV1 = 14;

// Sound parameters in the original's units. Comments give the offset in the
// original parameter struct (or pattern memory for the effects) and the
// default set by its constructor 0x40c720 / 0x40c640.
struct ParamsV1 {
    int32_t accent = 200;          // +0x08
    uint8_t osc1Type = 1;          // +0x0c: 0 saw/pulse, 1 saw, 2 square
    uint8_t osc2Type = 0;          // +0x0d: 0 sine (octave up), 1 saw, 2 noise
    int32_t osc2Double = 0;        // +0x10: osc2 an octave up, free running
    int32_t tune = 0;              // +0x14: knob - 128
    int32_t volume = 100;          // +0x18
    int32_t oscMix = 0x80;         // +0x1c: 0 = osc1 only
    int32_t attack = 0xff;         // +0x20
    int32_t decay = 0x80;          // +0x24
    int32_t modulation = 0x80;     // +0x28: 0 = envelope only, 255 = Cut Off only
    int32_t cutoff = 0x40;         // +0x2c
    int32_t resonance = 0x40;      // +0x30
    int32_t distortionAmount = 0x40; // pattern memory +0xb14: wet amount (0 = off)
    int32_t distortionCurve = 0x40;  // +0xb18: saturation of the log curve
    int32_t delayTime = 0;         // +0xb1c (x4 ms)
    int32_t delayGain = 0x40;      // +0xb20
    int32_t delayFeedback = 0;     // +0xb24
};

// 0 plays the selected pattern, 1 steps from rangeStart to rangeEnd, 2 picks
// at random in that range.
enum class PlayModeV1 : int32_t { Single = 0, Sequence = 1, Random = 2 };

// The bassline voice and its sequencer (original class with vtable 0x412ea0:
// init 0x40c870, render 0x40c910).
class EngineV1
{
public:
    EngineV1();

    // Original init 0x40c870 (called when the synth is started or a machine
    // state is loaded): snaps tune, cutoff, modulation and resonance to their
    // targets and re-initialises the filter at the next block.
    void reset();

    ParamsV1 params;
    std::array<Pattern, kNumPatternsV1> patterns{};
    int32_t rangeStart = 0;        // +0xa8c
    int32_t rangeEnd = 0;          // +0xa90
    PlayModeV1 playMode = PlayModeV1::Single;

    void setTempo(float bpm) { tempo = bpm; }
    float getTempo() const { return tempo; }
    // Tempo knob 0..255 as stored in .h30 files (60..250 BPM).
    void setTempoKnob(int knob);

    void setRunning(bool on) { running = on; }
    bool isRunning() const { return running; }
    void selectPattern(int index);
    int currentPatternIndex() const { return currentPattern; }
    int currentStep() const { return step; }
    // Back to step 0 (original MIDI "start").
    void rewind();

    // Host synchronisation. `p` is the position at the start of the next
    // block, 2^16 per step and 2^20 per bar; the sequencer counter is set so
    // that the block behaves as in the free-running original at that position
    // (a step starts at the first block strictly after its boundary). Returns
    // kNewStep / kNewBar when the block starts a step / a bar.
    static constexpr int kNewStep = 1, kNewBar = 2;
    int setNextPosition(uint32_t p);
    // Makes the next setNextPosition() trigger the current step.
    void forceTrigger() { forceNext = true; }
    // Sequencer increment per block at the current tempo (original +0x10).
    int32_t blockIncrement() const;

    // Live playing, applied at the next block like a sequencer step (notes use
    // the pattern numbering; `slide` glides from the current pitch).
    void noteOn(int note, bool accent, bool slide);
    void noteOff();

    Random& random() { return rng; }

    // Renders `blocks` blocks of 64 samples: the voice track before the
    // output stage. The effects run once over the whole call, as in the
    // original's audio periods.
    void render(int16_t* out, unsigned blocks);

    // Output stage of the original (0x405df0 / 0x406d70): track gain
    // volume << 8 split by the centre pan, 128/256 to the left channel and
    // 129/256 to the right one.
    static void applyVolume(int16_t* buf, unsigned n, int32_t volume, bool rightChannel = false);

    struct DebugState {
        uint32_t pos, level, phase1, inc1, phase2, inc2, driftPhase;
        int32_t freq1, freq2, slideRate, step, pattern;
        uint32_t cutoffSmooth, modSmooth, resSmooth;
        uint8_t stage, note, fine, target, peak;
        float cutoffHz, f, damping;
    };
    DebugState debugState() const;
    const std::array<int32_t, 257>& debugDistortionTable(int curve)
    {
        buildDistortionTable(curve);
        return distTable;
    }

private:
    struct Voice {                // note state, original +0x40 (osc1) / +0x54 (osc2)
        uint8_t flags = 0;        // bit 0 active, bit 1 recompute increment
        uint8_t pan = 0;
        uint16_t amplitude = 0x100;
        uint32_t aux = 0;
        uint8_t note = 0;
        int8_t fine = 0;
        uint32_t phase = 0;
        uint32_t increment = 0;
    };
    struct Filter {               // original +0xa0 (0x401340..0x4013f0)
        int32_t rate = 0;
        float f = 0, damping = 0;
        float b1 = 0, l1 = 0, b2 = 0, l2 = 0;
        void process(int16_t* buf, unsigned n);
    };

    void renderBlock(int16_t* t0);
    void sequencer();
    void triggerStep();
    void stepAt(const Step& s);
    void changePattern(int index);
    const Step& stepData(int pattern, int index) const;
    void oscillator(uint8_t kind, int32_t freq, Voice& v, int16_t* buf);
    int32_t pitchDistance(uint8_t target) const;
    void movePitch(int32_t amount);
    void buildDistortionTable(int curve);

    uint8_t paramFlags = 7;       // +0x00 of the parameter struct
    uint8_t lastOsc1 = 0xff, lastOsc2 = 0xff;
    int32_t lastTempoKnob = -1;

    // Sequencer.
    bool running = false;         // +0x04
    bool stopAtEnd = false;       // +0x08
    int32_t pos = 0;              // +0x0c, 2^16 per step, relative to the step
    int32_t inc = 0x82;           // +0x10
    int32_t blocksPerStep = 0;    // +0x14
    float tempo = 100.0f;         // +0x18
    int currentPattern = 0;       // +0xa80
    int step = 0;                 // +0xa88
    bool forceNext = false;
    bool pendingTrigger = false;
    uint32_t syncStep = 0xffffffffu;
    bool liveNote = false;
    bool liveOff = false;
    Step live;

    // Voice.
    int32_t slideRate = 0;        // +0x1c
    uint8_t targetNote = 0;       // +0x20
    uint8_t displayNote = 0;      // +0x21
    uint8_t osc1Kind = 3, osc2Kind = 1; // +0x28, +0x34
    int32_t freq1 = 0, freq2 = 0; // +0x2c, +0x38
    uint8_t envStage = 0;         // +0x3c
    uint8_t envPeak = 0;          // +0x3e
    Voice voice1, voice2;
    uint32_t level = 0;           // +0x6c
    int32_t tuneSmooth = 0;       // +0x70
    uint32_t driftPhase = 0;      // +0x9c
    Filter filter;                // +0xa0
    float cutoffHz = 0;           // +0xc4
    uint32_t cutoffSmooth = 0;    // +0xc0
    uint32_t modSmooth = 0;       // +0xcc
    uint32_t resSmooth = 0;       // +0xd0

    // Effects.
    std::array<int32_t, 257> distTable{};  // original global 0x4199c8
    int distCurve = -1;
    Delay delay;                  // +0x74

    Random rng;
    std::array<int16_t, kBlockSizeV1> bufB{};
    std::array<Pattern, 1> bogus{};
};

} // namespace rd
