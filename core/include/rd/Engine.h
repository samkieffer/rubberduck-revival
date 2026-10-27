#pragma once

#include <array>
#include <cstdint>
#include <functional>

#include "rd/Delay.h"
#include "rd/LadderFilter.h"
#include "rd/Random.h"
#include "rd/Shaper.h"
#include "rd/StateVariableFilter.h"

namespace rd {

constexpr int kSampleRate = 44100;
constexpr unsigned kBlockSize = 128;
constexpr int kNumPatterns = 16;
constexpr int kStepsPerPattern = 16;
constexpr int kSongLength = 99;

// Sound parameters, in the original's units (mostly 0..255 knob values).
// Field comments give the original parameter id and engine-struct offset.
struct Params {
    int32_t accent = 0xcc;        // id 3,  +0x08
    uint8_t osc1Type = 1;         // id 4,  +0x0c: 0 triangle, 1 saw/pulse, 2 saw, 3 square
    uint8_t osc2Type = 1;         // id 5,  +0x0d: 0 sine (octave up), 1 saw, 2 noise
    int32_t osc2Double = 1;       // id 6,  +0x10: osc2 one octave up, free running
    int32_t oscMix = 0;           // id 7,  +0x14: 0 = osc1 only
    int32_t tune = 0;             // id 2,  +0x18: knob - 128
    int32_t volume = 0x80;        // id 1,  +0x1c
    int32_t attack = 0xff;        // id 8,  +0x20
    int32_t decay = 0x80;         // id 9,  +0x24
    int32_t modulation = 0x80;    // id 11, +0x28: envelope-to-cutoff curve
    int32_t cutoff = 0x80;        // id 10, +0x2c
    int32_t resonance = 0x40;     // id 12, +0x30
    int32_t modification = 1;     // id 13, +0x34: 1 = two-branch filter
    int32_t tweak = 0;            // id 16, +0x38: rectifying drive
    int32_t tweet = 1;            // id 17, +0x3c: band-pass an octave up
    int32_t drive = 0x40;         // id 15, +0x40
    int32_t squelch = 0x40;       // id 14, +0x44
    // Global effects (pattern memory +0xdac..+0xdbc).
    int32_t distortionGain = 0;   // id 22, +0xdac: wet amount
    int32_t distortionDrive = 0x80; // id 21, +0xdb0: saturation (filter coefficient)
    int32_t delayTime = 0;        // id 18, +0xdb4 (x4 ms)
    int32_t delayGain = 0x80;     // id 19, +0xdb8
    int32_t delayFeedback = 0x80; // id 20, +0xdbc
};

struct Step {
    int8_t note = -2;             // -2 rest, -1 note off, else MIDI-like note number
    int32_t accent = 0;
    int32_t slide = 0;
};

struct Pattern {
    std::array<Step, kStepsPerPattern> steps{};
};

enum class PlayMode : int32_t { Single = 0, Sequence = 1, Shuffle = 2 };

// The bassline voice and its sequencer (original class with vtable 0x4352d8,
// render routine 0x408ca0), processing 128-sample blocks at 44.1 kHz.
class Engine
{
public:
    Engine();

    // Snaps the smoothed parameters (tune, cutoff, modulation, resonance) to
    // their targets, as the original does when a machine state is loaded.
    void resetSmoothing();

    Params params;
    std::array<Pattern, kNumPatterns> patterns{};
    std::array<uint8_t, kSongLength> song{};
    int32_t songLength = 1;
    PlayMode playMode = PlayMode::Single;

    void setTempo(float bpm) { tempo = bpm; }
    float getTempo() const { return tempo; }
    // Tempo knob 0..255 as stored in .h30 files (60..250 BPM).
    void setTempoKnob(int knob);

    void setRunning(bool on) { running = on; }
    bool isRunning() const { return running; }
    void selectPattern(int index);
    int currentPatternIndex() const { return currentPattern; }
    int currentStep() const { return step; }
    // Restart at the beginning of a bar (original transport "start" event).
    void rewind();
    // Sequencer position: 2^20 per bar, 2^16 per step.
    uint32_t position() const { return pos; }
    uint32_t previousPosition() const { return prevPos; }
    void setPosition(uint32_t p);
    // Position for the next block, e.g. from the host transport. A new step
    // or bar is detected against the previous block as usual.
    void setNextPosition(uint32_t p) { pos = p; }
    // Makes the next block treat its position as a new step and a new bar.
    void forceTrigger() { prevPos = pos ^ 0xfff00000u; }
    // Sequencer increment per block at the current tempo.
    uint32_t blockIncrement() const;

    // Live playing (bypassing the pattern sequencer), applied at the next block
    // like the original's MIDI input. Notes use the pattern numbering
    // (48 = "C-4" = MIDI 24); `slide` glides from the current pitch.
    void noteOn(int note, bool accent, bool slide);
    void noteOff();

    Random& random() { return rng; }

    // Renders `blocks` blocks of 128 samples. `out` receives the voice signal
    // before the master volume, i.e. the original's output track samples.
    void render(int16_t* out, unsigned blocks);
    // Internal state snapshot, for comparison against the original.
    struct DebugState {
        uint32_t pos, env, phase1, inc1, phase2, inc2, cutoffSmooth, resoSmooth, driftPhase;
        uint8_t stage, note, fine, target;
        int32_t base1, base2, slideRate;
        float cutoffHz;
    };
    DebugState debugState() const
    {
        return {pos, env, voice1.phase, voice1.increment, voice2.phase, voice2.increment, cutoffSmooth, resoSmooth,
                driftPhase, envStage, voice1.note, static_cast<uint8_t>(voice1.fine), targetNote, osc1Base, osc2Base,
                slideRate, cutoffHz};
    }

    const Delay& debugDelay() const { return delay; }
    const Shaper& debugDistortion() const { return distortion; }
    const Shaper& debugDrive() const { return drive; }
    const StateVariableFilter& debugLowPass() const { return lowPass; }
    const StateVariableFilter& debugBandPass() const { return bandPass; }

    // Optional capture of intermediate buffers (osc2 output, second branch), per block.
    int16_t* debugOsc2 = nullptr;
    int16_t* debugBranch = nullptr;
    int16_t* debugBranchInput = nullptr;
    unsigned debugBlock = 0;

    // Master volume of the output stage (original output mixer, left channel).
    uint32_t outputGain() const { return static_cast<uint32_t>(params.volume) << 7; }

private:
    struct Voice {
        uint8_t flags = 0;        // bit 0 active, bit 1 recompute increment
        uint8_t pan = 0;
        uint16_t amplitude = 0x100;
        uint32_t aux = 0;
        uint8_t note = 0;
        int8_t fine = 0;
        uint16_t pad = 0;
        uint32_t phase = 0;
        uint32_t increment = 0;
    };

    void renderBlock(int16_t* out);
    void triggerStep(const Step& s, uint32_t inc);
    void oscillator(int type, int32_t base, Voice& v, int16_t* buf);
    int32_t pitchDistance(const Voice& v, uint8_t target) const;
    void movePitch(Voice& v, int32_t amount);
    void applyOscTypes();

    // Sequencer / transport.
    bool running = false;
    uint32_t pos = 0;
    uint32_t prevPos = 0xffffffffu;
    float tempo = 120.0f;
    int currentPattern = 0;
    int songPos = 0;
    int step = 0;

    // Voice.
    uint8_t targetNote = 0;
    uint8_t currentNote = 0;
    int32_t slideRate = 0;
    uint32_t slideAcc = 0;
    int osc1Wave = 6, osc2Wave = 3;
    int32_t osc1Base = 0x4168f5, osc2Base = 0x4168f5;
    uint8_t envStage = 0;
    uint8_t envPeak = 0;
    uint32_t env = 0;
    int32_t tuneSmooth = 0;
    Voice voice1, voice2;
    uint32_t driftPhase = 0;

    // Filters.
    uint32_t cutoffSmooth = 0;
    float cutoffHz = 0;
    uint32_t modSmooth = 0;
    uint32_t resoSmooth = 0;
    StateVariableFilter lowPass;   // +0x4fc
    StateVariableFilter bandPass;  // +0x520
    LadderFilter ladder;           // +0x4c0
    Shaper drive;                  // +0x2a4
    Shaper distortion;             // +0x090
    Delay delay;                   // +0x070

    Random rng;
    std::array<int16_t, kBlockSize> bufB{}, bufC{};
    uint8_t lastOsc1Type = 0xff, lastOsc2Type = 0xff;
};

} // namespace rd
