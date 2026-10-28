#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <atomic>
#include <vector>

#include "rd/Engine.h"
#include "rd/EngineV1.h"
#include "rd/H30File.h"

// Patterns and song, edited by the UI and copied to the engine on the audio
// thread under a spin lock.
struct SequenceData {
    std::array<rd::Pattern, rd::kNumPatterns> patterns{};
    std::array<uint8_t, rd::kSongLength> song{};
    int songLength = 1;
};

class RubberDuckProcessor : public juce::AudioProcessor
{
public:
    enum class Source { Patterns = 0, MidiNotes = 1 };
    enum class Quantize { NextBar = 0, NextStep = 1, Immediate = 2 };
    // Choice parameter "engine", in chronological order.
    enum class EngineKind { V102 = 0, V212 = 1 };
    // Live playing (Source = MIDI notes): MIDI 24 (C1) is pattern note 48
    // ("C-4"); velocities from 100 play accented, overlapping notes slide.
    static constexpr int kMidiToPatternNote = 24;
    static constexpr int kAccentVelocity = 100;

    RubberDuckProcessor();
    ~RubberDuckProcessor() override = default;

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 1.2; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // UI helpers (message thread).
    SequenceData getSequence() const;
    void setSequence(const SequenceData& data);
    bool importH30(const juce::File& file, juce::String& error);
    bool exportH30(const juce::File& file) const;

    int getPlayingStep() const { return playingStep.load(); }
    int getPlayingPattern() const { return playingPattern.load(); }
    bool isSequencerRunning() const { return sequencerRunning.load(); }
    float getHostBpm() const { return lastBpm.load(); }
    EngineKind getEngineKind() const { return static_cast<EngineKind>(paramInt(pEngine)); }
    int numPatterns() const { return getEngineKind() == EngineKind::V102 ? rd::kNumPatternsV1 : rd::kNumPatterns; }
    // 32-step patterns: in Single mode the selected pattern plays with its pair
    // (0+1, 2+3...), one per bar, aligned to the host's odd and even bars.
    bool pairMode() const { return paramInt(pLength32) != 0; }

    juce::AudioProcessorValueTreeState apvts;

private:
    struct PendingMidi {
        int64_t hostTime;
        juce::MidiMessage message;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void readParameters();
    void renderEngineBlock(int64_t blockHostTime, int64_t bufferStart, double ppqAtBufferStart, double bpm,
                           bool hostPlaying);
    // Sequencer position for a block (2^16 per step); false before the song start.
    bool hostPosition(int64_t blockHostTime, int64_t bufferStart, double ppqAtBufferStart, double bpm,
                      uint32_t& position) const;
    void handleMidi(const juce::MidiMessage& m);
    // Pattern the engine should play for the selection `index` at `bar`.
    int patternFor(int index, bool positioned, uint32_t bar) const;
    // Makes the active engine play `index`. At a bar start the v1.02 sequencer
    // is re-synchronised so that its own end-of-pattern advance (Sequence and
    // Random modes) does not skip the pattern just selected.
    void applyPattern(int index, bool barStart, uint32_t target);
    int paramInt(std::atomic<float>* p) const { return static_cast<int>(std::lround(p->load())); }

    rd::Engine engine;      // v2.12
    rd::EngineV1 engineV1;  // v1.02
    bool v1Active = false;

    juce::SpinLock sequenceLock;
    SequenceData sequence;
    std::atomic<bool> sequenceDirty{true};
    std::atomic<bool> snapSmoothing{true};  // after loading a machine state, as the original does
    std::atomic<bool> loadPending{false};   // a .h30 was loaded: switch to its pattern at once

    // Parameter handles.
    std::atomic<float>* pVolume = nullptr;
    std::atomic<float>* pTune = nullptr;
    std::atomic<float>* pAccent = nullptr;
    std::atomic<float>* pOsc1 = nullptr;
    std::atomic<float>* pOsc2 = nullptr;
    std::atomic<float>* pOsc2Double = nullptr;
    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pAttack = nullptr;
    std::atomic<float>* pDecay = nullptr;
    std::atomic<float>* pCutoff = nullptr;
    std::atomic<float>* pModulation = nullptr;
    std::atomic<float>* pResonance = nullptr;
    std::atomic<float>* pModification = nullptr;
    std::atomic<float>* pSquelch = nullptr;
    std::atomic<float>* pDrive = nullptr;
    std::atomic<float>* pTweak = nullptr;
    std::atomic<float>* pTweet = nullptr;
    std::atomic<float>* pDelayTime = nullptr;
    std::atomic<float>* pDelayGain = nullptr;
    std::atomic<float>* pDelayFeedback = nullptr;
    std::atomic<float>* pDistGain = nullptr;
    std::atomic<float>* pDistDrive = nullptr;
    std::atomic<float>* pPattern = nullptr;
    std::atomic<float>* pPlayMode = nullptr;
    std::atomic<float>* pSource = nullptr;
    std::atomic<float>* pQuantize = nullptr;
    std::atomic<float>* pEngine = nullptr;
    std::atomic<float>* pRangeStart = nullptr;
    std::atomic<float>* pRangeEnd = nullptr;
    std::atomic<float>* pLength32 = nullptr;
    std::atomic<float> lastBpm{120.0f};

    // Host / engine time bookkeeping. The engines run at 44.1 kHz in blocks of
    // 128 (v2.12) or 64 (v1.02) samples; their output is resampled when the
    // host runs at another rate.
    double hostRate = 44100.0;
    bool direct = true;
    int64_t hostSamplePos = 0;
    std::vector<float> fifo;          // engine output not yet consumed
    size_t fifoRead = 0;
    juce::WindowedSincInterpolator interpolator;
    double latencyHost = 0;
    std::vector<int16_t> blockBuffer;
    std::vector<float> scratch;

    bool wasPlaying = false;
    bool startPending = false;
    std::vector<PendingMidi> pendingMidi;
    std::vector<int> heldNotes;

    int lastPatternParam = 0;
    int desiredPattern = 0;
    int activePattern = 0;  // selection in effect (the pair's first or second half in 32-step mode)
    bool patternChangePending = false;

    std::atomic<int> playingStep{-1};
    std::atomic<int> playingPattern{0};
    std::atomic<bool> sequencerRunning{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RubberDuckProcessor)
};
