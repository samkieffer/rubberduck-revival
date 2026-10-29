#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "PluginEditor.h"
#include "rd/Mixing.h"

namespace {

constexpr int kAccentVelocity = RubberDuckProcessor::kAccentVelocity;
constexpr int kMidiToPatternNote = RubberDuckProcessor::kMidiToPatternNote;
constexpr int kPatternSelectLowNote = 36; // in pattern mode, C2..D#3 select patterns 1..16

// Osc1 choice (v2.12 numbering) to v1.02's three waveforms and back; v1.02 has
// no triangle and plays it as a saw.
constexpr uint8_t kOsc1ToV1[4] = {1, 0, 1, 2};
constexpr int kOsc1FromV1[3] = {1, 2, 3};

int tempoKnobFor(float bpm)
{
    return juce::jlimit(0, 255, static_cast<int>(std::lround((bpm - 60.0f) * 255.0f / 190.0f)));
}

} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout RubberDuckProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto knob = [&p](const char* id, const char* name, int min, int max, int def) {
        p.push_back(std::make_unique<AudioParameterInt>(ParameterID{id, 1}, name, min, max, def));
    };
    auto toggle = [&p](const char* id, const char* name, bool def) {
        p.push_back(std::make_unique<AudioParameterBool>(ParameterID{id, 1}, name, def));
    };
    auto choice = [&p](const char* id, const char* name, StringArray items, int def) {
        p.push_back(std::make_unique<AudioParameterChoice>(ParameterID{id, 1}, name, items, def));
    };

    // Same ranges and defaults as the original v2.12 (see docs/reverse-engineering.md);
    // the v1.02 engine reads the same parameters where it has them.
    choice("engine", "Engine", {"v1.02 (1997)", "v2.12 (2000)"}, 0);
    knob("cutoff", "Cut Off", 0, 255, 128);
    knob("resonance", "Resonance", 0, 255, 64);
    knob("modulation", "Modulation", 0, 255, 128);
    knob("attack", "Attack", 0, 255, 255);
    knob("decay", "Decay", 0, 255, 128);
    knob("accent", "Accent", 0, 255, 204);
    knob("squelch", "Squelch", 0, 255, 64);
    knob("drive", "Drive", 0, 255, 64);
    toggle("modification", "Filter Modification", true);
    toggle("tweak", "Tweak", false);
    toggle("tweet", "Tweet", true);
    choice("osc1", "Osc1 Type", {"Triangle", "Saw/Pulse", "Saw", "Square"}, 1);
    choice("osc2", "Osc2 Type", {"Sine", "Saw", "Noise"}, 1);
    toggle("osc2double", "Osc2 Freq x2", true);
    knob("mix", "Osc1/Osc2 Mix", 0, 255, 0);
    knob("tune", "Tune", -128, 127, 0);
    knob("volume", "Volume", 0, 255, 128);
    knob("delaytime", "Delay Time", 0, 255, 0);
    knob("delaygain", "Delay Gain", 0, 255, 128);
    knob("delayfeedback", "Delay Feedback", 0, 255, 128);
    knob("distgain", "Distortion Gain", 0, 255, 0);
    knob("distdrive", "Distortion Drive", 0, 255, 128);
    knob("pattern", "Pattern", 1, 16, 1);
    choice("playmode", "Play Mode", {"Single", "Sequence", "Shuffle"}, 0);
    choice("source", "Sequencer Source", {"Patterns", "MIDI notes"}, 0);
    choice("quantize", "Pattern Change", {"Next bar", "Next step", "Immediate"}, 0);
    // v1.02 sequence and random modes play a range of patterns (not the song).
    const StringArray v1Patterns{"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "A", "B", "C", "D"};
    choice("rangestart", "Range From (v1.02)", v1Patterns, 0);
    choice("rangeend", "Range To (v1.02)", v1Patterns, 0);
    toggle("length32", "32-Step Patterns", false);
    return {p.begin(), p.end()};
}

RubberDuckProcessor::RubberDuckProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", createLayout())
{
    auto raw = [this](const char* id) { return apvts.getRawParameterValue(id); };
    pVolume = raw("volume");
    pTune = raw("tune");
    pAccent = raw("accent");
    pOsc1 = raw("osc1");
    pOsc2 = raw("osc2");
    pOsc2Double = raw("osc2double");
    pMix = raw("mix");
    pAttack = raw("attack");
    pDecay = raw("decay");
    pCutoff = raw("cutoff");
    pModulation = raw("modulation");
    pResonance = raw("resonance");
    pModification = raw("modification");
    pSquelch = raw("squelch");
    pDrive = raw("drive");
    pTweak = raw("tweak");
    pTweet = raw("tweet");
    pDelayTime = raw("delaytime");
    pDelayGain = raw("delaygain");
    pDelayFeedback = raw("delayfeedback");
    pDistGain = raw("distgain");
    pDistDrive = raw("distdrive");
    pPattern = raw("pattern");
    pPlayMode = raw("playmode");
    pSource = raw("source");
    pQuantize = raw("quantize");
    pEngine = raw("engine");
    pRangeStart = raw("rangestart");
    pRangeEnd = raw("rangeend");
    pLength32 = raw("length32");

    // A fresh instance starts with empty patterns (all rests).
    readParameters();
    engine.resetSmoothing();
    blockBuffer.resize(rd::kBlockSize);
}

bool RubberDuckProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void RubberDuckProcessor::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock)
{
    hostRate = sampleRate;
    direct = std::abs(sampleRate - rd::kSampleRate) < 0.5;
    interpolator.reset();
    fifo.clear();
    fifo.reserve(16384);
    fifoRead = 0;
    hostSamplePos = 0;
    pendingMidi.clear();
    pendingMidi.reserve(1024);
    heldNotes.clear();
    scratch.assign(static_cast<size_t>(std::max(maximumExpectedSamplesPerBlock, 1)), 0.0f);
    const double ratio = rd::kSampleRate / hostRate;
    latencyHost = direct ? 0.0 : juce::WindowedSincInterpolator::getBaseLatency() / ratio;
    setLatencySamples(static_cast<int>(std::lround(latencyHost)));
}

void RubberDuckProcessor::readParameters()
{
    rd::Params& P = engine.params;
    P.volume = paramInt(pVolume);
    P.tune = paramInt(pTune);
    P.accent = paramInt(pAccent);
    P.osc1Type = static_cast<uint8_t>(paramInt(pOsc1));
    P.osc2Type = static_cast<uint8_t>(paramInt(pOsc2));
    P.osc2Double = paramInt(pOsc2Double);
    P.oscMix = paramInt(pMix);
    P.attack = paramInt(pAttack);
    P.decay = paramInt(pDecay);
    P.cutoff = paramInt(pCutoff);
    P.modulation = paramInt(pModulation);
    P.resonance = paramInt(pResonance);
    P.modification = paramInt(pModification);
    P.squelch = paramInt(pSquelch);
    P.drive = paramInt(pDrive);
    P.tweak = paramInt(pTweak);
    P.tweet = paramInt(pTweet);
    P.delayTime = paramInt(pDelayTime);
    P.delayGain = paramInt(pDelayGain);
    P.delayFeedback = paramInt(pDelayFeedback);
    P.distortionGain = paramInt(pDistGain);
    P.distortionDrive = paramInt(pDistDrive);
    engine.playMode = static_cast<rd::PlayMode>(paramInt(pPlayMode));

    rd::ParamsV1& Q = engineV1.params;
    Q.volume = P.volume;
    Q.tune = P.tune;
    Q.accent = P.accent;
    Q.osc1Type = kOsc1ToV1[P.osc1Type & 3];
    Q.osc2Type = P.osc2Type;
    Q.osc2Double = P.osc2Double;
    Q.oscMix = P.oscMix;
    Q.attack = P.attack;
    Q.decay = P.decay;
    Q.cutoff = P.cutoff;
    Q.modulation = P.modulation;
    Q.resonance = P.resonance;
    Q.delayTime = P.delayTime;
    Q.delayGain = P.delayGain;
    Q.delayFeedback = P.delayFeedback;
    Q.distortionAmount = P.distortionGain;
    Q.distortionCurve = P.distortionDrive;
    engineV1.playMode = static_cast<rd::PlayModeV1>(paramInt(pPlayMode));
    engineV1.rangeStart = paramInt(pRangeStart);
    engineV1.rangeEnd = paramInt(pRangeEnd);
}

void RubberDuckProcessor::handleMidi(const juce::MidiMessage& m)
{
    const auto source = static_cast<Source>(paramInt(pSource));
    if (m.isProgramChange()) {
        if (m.getProgramChangeNumber() < numPatterns()) {
            desiredPattern = m.getProgramChangeNumber();
            patternChangePending = true;
        }
        return;
    }
    if (source == Source::Patterns) {
        if (m.isNoteOn()) {
            const int index = m.getNoteNumber() - kPatternSelectLowNote;
            if (index >= 0 && index < numPatterns()) {
                desiredPattern = index;
                patternChangePending = true;
            }
        }
        return;
    }

    if (m.isNoteOn()) {
        const int note = m.getNoteNumber();
        const bool legato = !heldNotes.empty();
        heldNotes.erase(std::remove(heldNotes.begin(), heldNotes.end(), note), heldNotes.end());
        heldNotes.push_back(note);
        if (v1Active)
            engineV1.noteOn(note + kMidiToPatternNote, m.getVelocity() >= kAccentVelocity, legato);
        else
            engine.noteOn(note + kMidiToPatternNote, m.getVelocity() >= kAccentVelocity, legato);
    } else if (m.isNoteOff()) {
        const int note = m.getNoteNumber();
        const bool wasLast = !heldNotes.empty() && heldNotes.back() == note;
        heldNotes.erase(std::remove(heldNotes.begin(), heldNotes.end(), note), heldNotes.end());
        if (heldNotes.empty()) {
            engine.noteOff();
            engineV1.noteOff();
        } else if (wasLast) {
            if (v1Active)
                engineV1.noteOn(heldNotes.back() + kMidiToPatternNote, false, true);
            else
                engine.noteOn(heldNotes.back() + kMidiToPatternNote, false, true);
        }
    } else if (m.isAllNotesOff() || m.isAllSoundOff()) {
        heldNotes.clear();
        engine.noteOff();
        engineV1.noteOff();
    }
}

int RubberDuckProcessor::patternFor(int index, bool positioned, uint32_t bar) const
{
    if (!pairMode() || paramInt(pPlayMode) != 0)
        return index;
    return (index & ~1) + (positioned ? static_cast<int>(bar & 1) : 0);
}

void RubberDuckProcessor::applyPattern(int index, bool barStart, uint32_t target)
{
    if (!v1Active) {
        engine.selectPattern(index);
        return;
    }
    engineV1.selectPattern(index);
    if (barStart && engineV1.isRunning()) {
        // The block was set up to cross the end of the previous pattern, where
        // Sequence and Random modes advance by themselves. Start it on step 0
        // of the selected pattern instead (same samples as the natural wrap).
        engineV1.forceTrigger();
        engineV1.setNextPosition(target);
    }
}

bool RubberDuckProcessor::hostPosition(int64_t blockHostTime, int64_t bufferStart, double ppqAtBufferStart, double bpm,
                                       uint32_t& position) const
{
    const double ppq = ppqAtBufferStart + static_cast<double>(blockHostTime - bufferStart) / hostRate * bpm / 60.0;
    if (ppq < 0.0)
        return false;
    // 2^18 per quarter note: 2^16 per sixteenth step, 2^20 per bar.
    position = static_cast<uint32_t>(static_cast<int64_t>(std::floor(ppq * 262144.0)));
    return true;
}

void RubberDuckProcessor::renderEngineBlock(int64_t blockHostTime, int64_t bufferStart, double ppqAtBufferStart,
                                            double bpm, bool hostPlaying)
{
    const bool useV1 = getEngineKind() == EngineKind::V102;
    if (useV1 != v1Active) {
        // Engine switch: the other engine stops, the new one starts on the next block.
        v1Active = useV1;
        engine.setRunning(false);
        engineV1.setRunning(false);
        heldNotes.clear();
        if (useV1)
            engine.noteOff();
        else
            engineV1.noteOff();
    }

    // MIDI events due by the time this block is heard.
    size_t used = 0;
    while (used < pendingMidi.size() && pendingMidi[used].hostTime <= blockHostTime)
        handleMidi(pendingMidi[used++].message);
    if (used > 0)
        pendingMidi.erase(pendingMidi.begin(), pendingMidi.begin() + static_cast<long>(used));

    readParameters();
    if (snapSmoothing.exchange(false)) {
        engine.resetSmoothing();
        engineV1.reset();
    }

    const int patternParam = paramInt(pPattern) - 1;
    if (patternParam != lastPatternParam) {
        lastPatternParam = patternParam;
        desiredPattern = patternParam;
        patternChangePending = true;
    }

    bool newBar = false, newStep = false, positioned = false;
    uint32_t target = 0;
    const bool sequencing = static_cast<Source>(paramInt(pSource)) == Source::Patterns && hostPlaying;
    if (sequencing && hostPosition(blockHostTime, bufferStart, ppqAtBufferStart, bpm, target)) {
        positioned = true;
        if (v1Active) {
            if (startPending || !engineV1.isRunning()) {
                engineV1.forceTrigger();
                engineV1.setRunning(true);
                startPending = false;
            }
            const int flags = engineV1.setNextPosition(target);
            newStep = (flags & rd::EngineV1::kNewStep) != 0;
            newBar = (flags & rd::EngineV1::kNewBar) != 0;
        } else {
            engine.setNextPosition(target);
            if (startPending || !engine.isRunning()) {
                engine.forceTrigger();
                engine.setRunning(true);
                startPending = false;
            }
            newBar = (target >> 20) != (engine.previousPosition() >> 20);
            newStep = (target >> 16) != (engine.previousPosition() >> 16);
        }
    } else if (!sequencing) {
        engine.setRunning(false);
        engineV1.setRunning(false);
    }

    const bool running = v1Active ? engineV1.isRunning() : engine.isRunning();
    const uint32_t bar = target >> 20;
    bool applied = false;
    if (loadPending.load() && !sequenceDirty.load()) {
        // A .h30 was just loaded: play its pattern now. Waiting for the next bar
        // would finish the bar with the new file's pattern at the old index.
        loadPending.store(false);
        desiredPattern = lastPatternParam = paramInt(pPattern) - 1;
        patternChangePending = false;
        activePattern = desiredPattern;
        int start = patternFor(activePattern, positioned, bar);
        if (!v1Active && positioned && engine.playMode == rd::PlayMode::Sequence)
            start = engine.song[static_cast<size_t>(bar % static_cast<uint32_t>(engine.songLength)) % rd::kSongLength];
        applyPattern(start, newBar, target);
        applied = true;
    } else if (patternChangePending) {
        const auto q = static_cast<Quantize>(paramInt(pQuantize));
        if (q == Quantize::Immediate || !running || (q == Quantize::NextBar && newBar)
            || (q == Quantize::NextStep && newStep)) {
            activePattern = desiredPattern;
            applyPattern(patternFor(activePattern, positioned, bar), newBar, target);
            patternChangePending = false;
            applied = true;
        }
    }
    if (!applied && newBar && pairMode() && paramInt(pPlayMode) == 0)
        applyPattern(patternFor(activePattern, positioned, bar), true, target);

    unsigned n;
    if (v1Active) {
        n = rd::kBlockSizeV1;
        engineV1.render(blockBuffer.data(), 1);
        rd::EngineV1::applyVolume(blockBuffer.data(), n, engineV1.params.volume);
    } else {
        n = rd::kBlockSize;
        engine.render(blockBuffer.data(), 1);
        // Output stage of the original (left channel): volume as a fixed-point gain.
        uint32_t gain = static_cast<uint32_t>(engine.params.volume) << 7;
        if (gain <= 0x7f)
            std::fill(blockBuffer.begin(), blockBuffer.end(), int16_t{0});
        else
            rd::mix::applyGain(blockBuffer.data(), rd::kBlockSize, gain);
    }

    for (unsigned i = 0; i < n; ++i)
        fifo.push_back(static_cast<float>(blockBuffer[i]) * (1.0f / 32768.0f));

    const bool nowRunning = v1Active ? engineV1.isRunning() : engine.isRunning();
    playingStep.store(nowRunning ? (v1Active ? engineV1.currentStep() : engine.currentStep()) : -1);
    playingPattern.store(v1Active ? engineV1.currentPatternIndex() : engine.currentPatternIndex());
    sequencerRunning.store(nowRunning);
}

void RubberDuckProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (static_cast<int>(scratch.size()) < n)
        scratch.resize(static_cast<size_t>(n));

    if (sequenceDirty.load()) {
        const juce::SpinLock::ScopedTryLockType lock(sequenceLock);
        if (lock.isLocked()) {
            engine.patterns = sequence.patterns;
            engine.song = sequence.song;
            engine.songLength = std::max(1, sequence.songLength);
            for (int i = 0; i < rd::kNumPatternsV1; ++i)
                engineV1.patterns[static_cast<size_t>(i)] = sequence.patterns[static_cast<size_t>(i)];
            sequenceDirty.store(false);
        }
    }

    double bpm = 120.0, ppq = 0.0;
    bool playing = false;
    if (hasOwnTransport()) {
        // Each engine computes the tempo from the knob as its original does.
        const int knob = tempoKnob.load();
        engine.setTempoKnob(knob);
        engineV1.setTempoKnob(knob);
        bpm = getEngineKind() == EngineKind::V102 ? engineV1.getTempo() : engine.getTempo();
        playing = transportPlaying.load();
        if (playing && !wasPlaying)
            ownPpq = 0.0;
        ppq = ownPpq;
    } else {
        if (auto* host = getPlayHead())
            if (auto position = host->getPosition()) {
                if (auto b = position->getBpm())
                    bpm = *b;
                if (auto q = position->getPpqPosition())
                    ppq = *q;
                playing = position->getIsPlaying();
            }
        engine.setTempo(static_cast<float>(bpm));
        engineV1.setTempo(static_cast<float>(bpm));
    }
    if (playing && !wasPlaying)
        startPending = true;
    wasPlaying = playing;
    lastBpm.store(static_cast<float>(bpm));

    for (const auto meta : midi)
        pendingMidi.push_back({hostSamplePos + meta.samplePosition, meta.getMessage()});
    midi.clear();

    float* out = scratch.data();
    if (direct) {
        int i = 0;
        while (i < n) {
            if (fifoRead >= fifo.size())
                renderEngineBlock(hostSamplePos + i, hostSamplePos, ppq, bpm, playing);
            const int take = std::min(static_cast<int>(fifo.size() - fifoRead), n - i);
            std::memcpy(out + i, fifo.data() + fifoRead, static_cast<size_t>(take) * sizeof(float));
            fifoRead += static_cast<size_t>(take);
            i += take;
        }
    } else {
        const double ratio = rd::kSampleRate / hostRate;
        const auto needed = static_cast<size_t>(std::ceil(n * ratio)) + 4;
        while (fifo.size() - fifoRead < needed) {
            const double queued = static_cast<double>(fifo.size() - fifoRead);
            const auto when = hostSamplePos + static_cast<int64_t>(std::llround(queued / ratio + latencyHost));
            renderEngineBlock(when, hostSamplePos, ppq, bpm, playing);
        }
        fifoRead += static_cast<size_t>(interpolator.process(ratio, fifo.data() + fifoRead, out, n));
    }
    if (fifoRead > 8192) {
        fifo.erase(fifo.begin(), fifo.begin() + static_cast<long>(fifoRead));
        fifoRead = 0;
    }

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.copyFrom(ch, 0, out, n);
    hostSamplePos += n;
    if (hasOwnTransport() && playing)
        ownPpq += static_cast<double>(n) / hostRate * bpm / 60.0;
}

SequenceData RubberDuckProcessor::getSequence() const
{
    const juce::SpinLock::ScopedLockType lock(sequenceLock);
    return sequence;
}

void RubberDuckProcessor::setSequence(const SequenceData& data)
{
    {
        const juce::SpinLock::ScopedLockType lock(sequenceLock);
        sequence = data;
    }
    sequenceDirty.store(true);
}

juce::File RubberDuckProcessor::getMachineFile() const
{
    const juce::SpinLock::ScopedLockType lock(machineFileLock);
    return machineFile;
}

void RubberDuckProcessor::setMachineFile(const juce::File& file)
{
    const juce::SpinLock::ScopedLockType lock(machineFileLock);
    machineFile = file;
}

namespace {

void setParam(juce::AudioProcessorValueTreeState& apvts, const char* id, float value)
{
    if (auto* p = apvts.getParameter(id))
        p->setValueNotifyingHost(p->convertTo0to1(value));
}

} // namespace

bool RubberDuckProcessor::importH30(const juce::File& file, juce::String& error)
{
    rd::H30File h30;
    std::string err;
    if (!rd::H30File::load(file.getFullPathName().toStdString(), h30, &err)) {
        error = err;
        return false;
    }
    SequenceData d;
    d.patterns = h30.patterns;
    d.song = h30.song;
    d.songLength = h30.songLength;
    setSequence(d);
    // The file's tempo, for the standalone app's own transport (a host keeps its own).
    if (h30.hasParams)
        setTempoKnob(h30.tempoKnob);

    // Files are played by the program that wrote them: versions 1-2 come from
    // v1.02, later ones from v2.x. Parameters are read as that version's loader does.
    const bool v1 = h30.version <= 2;
    setParam(apvts, "engine", static_cast<float>(v1 ? EngineKind::V102 : EngineKind::V212));
    if (v1) {
        rd::EngineV1 e;
        h30.applyTo(e);
        setParam(apvts, "rangestart", static_cast<float>(juce::jlimit(0, rd::kNumPatternsV1 - 1, e.rangeStart)));
        setParam(apvts, "rangeend", static_cast<float>(juce::jlimit(0, rd::kNumPatternsV1 - 1, e.rangeEnd)));
        setParam(apvts, "pattern", static_cast<float>(1 + e.currentPatternIndex()));
        if (h30.hasParams) {
            const rd::ParamsV1& p = e.params;
            setParam(apvts, "volume", static_cast<float>(p.volume));
            setParam(apvts, "tune", static_cast<float>(p.tune));
            setParam(apvts, "accent", static_cast<float>(p.accent));
            if (p.osc1Type < 3)
                setParam(apvts, "osc1", static_cast<float>(kOsc1FromV1[p.osc1Type]));
            if (p.osc2Type < 3)
                setParam(apvts, "osc2", static_cast<float>(p.osc2Type));
            setParam(apvts, "osc2double", p.osc2Double ? 1.0f : 0.0f);
            setParam(apvts, "mix", static_cast<float>(p.oscMix));
            setParam(apvts, "cutoff", static_cast<float>(p.cutoff));
            setParam(apvts, "attack", static_cast<float>(p.attack));
            setParam(apvts, "decay", static_cast<float>(p.decay));
            setParam(apvts, "modulation", static_cast<float>(p.modulation));
            setParam(apvts, "resonance", static_cast<float>(p.resonance));
            setParam(apvts, "distgain", static_cast<float>(p.distortionAmount & 0xff));
            setParam(apvts, "distdrive", static_cast<float>(p.distortionCurve & 0xff));
            setParam(apvts, "delaytime", static_cast<float>(p.delayTime));
            setParam(apvts, "delaygain", static_cast<float>(p.delayGain));
            setParam(apvts, "delayfeedback", static_cast<float>(p.delayFeedback));
            setParam(apvts, "playmode", static_cast<float>(e.playMode));
        }
        snapSmoothing.store(true);
        loadPending.store(true);
        return true;
    }

    if (h30.hasParams) {
        const rd::Params& p = h30.params;
        setParam(apvts, "tune", static_cast<float>(p.tune));
        setParam(apvts, "accent", static_cast<float>(p.accent));
        setParam(apvts, "osc1", static_cast<float>(p.osc1Type));
        setParam(apvts, "osc2", static_cast<float>(p.osc2Type));
        setParam(apvts, "osc2double", p.osc2Double ? 1.0f : 0.0f);
        setParam(apvts, "mix", static_cast<float>(p.oscMix));
        setParam(apvts, "cutoff", static_cast<float>(p.cutoff));
        setParam(apvts, "attack", static_cast<float>(p.attack));
        setParam(apvts, "decay", static_cast<float>(p.decay));
        setParam(apvts, "modulation", static_cast<float>(p.modulation));
        setParam(apvts, "resonance", static_cast<float>(p.resonance));
        setParam(apvts, "modification", p.modification ? 1.0f : 0.0f);
        if (h30.version >= 3) {
            setParam(apvts, "squelch", static_cast<float>(p.squelch));
            setParam(apvts, "drive", static_cast<float>(p.drive));
            setParam(apvts, "tweak", p.tweak ? 1.0f : 0.0f);
            setParam(apvts, "tweet", p.tweet ? 1.0f : 0.0f);
        }
        setParam(apvts, "distgain", static_cast<float>(p.distortionGain));
        setParam(apvts, "distdrive", static_cast<float>(p.distortionDrive));
        setParam(apvts, "delaytime", static_cast<float>(p.delayTime));
        setParam(apvts, "delaygain", static_cast<float>(p.delayGain));
        setParam(apvts, "delayfeedback", static_cast<float>(p.delayFeedback));
    }
    setParam(apvts, "pattern", static_cast<float>(1 + (h30.song[0] < rd::kNumPatterns ? h30.song[0] : 0)));
    snapSmoothing.store(true);
    loadPending.store(true);
    return true;
}

bool RubberDuckProcessor::exportH30(const juce::File& file) const
{
    const SequenceData d = getSequence();
    auto get = [this](const char* id) { return static_cast<int>(std::lround(apvts.getRawParameterValue(id)->load())); };
    const int knob = hasOwnTransport() ? getTempoKnob() : tempoKnobFor(lastBpm.load());
    rd::H30File h30;
    if (getEngineKind() == EngineKind::V102) {
        // Version 2, the v1.02 format.
        rd::EngineV1 snapshot;
        for (int i = 0; i < rd::kNumPatternsV1; ++i)
            snapshot.patterns[static_cast<size_t>(i)] = d.patterns[static_cast<size_t>(i)];
        snapshot.rangeStart = get("rangestart");
        snapshot.rangeEnd = get("rangeend");
        snapshot.playMode = static_cast<rd::PlayModeV1>(get("playmode"));
        rd::ParamsV1& Q = snapshot.params;
        Q.volume = get("volume");
        Q.tune = get("tune");
        Q.accent = get("accent");
        Q.osc1Type = kOsc1ToV1[get("osc1") & 3];
        Q.osc2Type = static_cast<uint8_t>(get("osc2"));
        Q.osc2Double = get("osc2double");
        Q.oscMix = get("mix");
        Q.cutoff = get("cutoff");
        Q.attack = get("attack");
        Q.decay = get("decay");
        Q.modulation = get("modulation");
        Q.resonance = get("resonance");
        Q.distortionAmount = get("distgain");
        Q.distortionCurve = get("distdrive");
        Q.delayTime = get("delaytime");
        Q.delayGain = get("delaygain");
        Q.delayFeedback = get("delayfeedback");
        const auto bytes = h30.serialize(snapshot, knob);
        return file.replaceWithData(bytes.data(), bytes.size());
    }

    rd::Engine snapshot;
    snapshot.patterns = d.patterns;
    snapshot.song = d.song;
    snapshot.songLength = d.songLength;
    rd::Params& P = snapshot.params;
    P.volume = get("volume");
    P.tune = get("tune");
    P.accent = get("accent");
    P.osc1Type = static_cast<uint8_t>(get("osc1"));
    P.osc2Type = static_cast<uint8_t>(get("osc2"));
    P.osc2Double = get("osc2double");
    P.oscMix = get("mix");
    P.cutoff = get("cutoff");
    P.attack = get("attack");
    P.decay = get("decay");
    P.modulation = get("modulation");
    P.resonance = get("resonance");
    P.modification = get("modification");
    P.squelch = get("squelch");
    P.drive = get("drive");
    P.tweak = get("tweak");
    P.tweet = get("tweet");
    P.distortionGain = get("distgain");
    P.distortionDrive = get("distdrive");
    P.delayTime = get("delaytime");
    P.delayGain = get("delaygain");
    P.delayFeedback = get("delayfeedback");
    const auto bytes = h30.serialize(snapshot, knob);
    return file.replaceWithData(bytes.data(), bytes.size());
}

void RubberDuckProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    const SequenceData d = getSequence();
    juce::MemoryBlock seq;
    for (const auto& pattern : d.patterns)
        for (const auto& step : pattern.steps) {
            const int8_t note = step.note;
            const uint8_t flags = static_cast<uint8_t>((step.accent ? 1 : 0) | (step.slide ? 2 : 0));
            seq.append(&note, 1);
            seq.append(&flags, 1);
        }
    seq.append(d.song.data(), d.song.size());
    const int32_t len = d.songLength;
    seq.append(&len, sizeof len);
    xml->setAttribute("sequence", seq.toBase64Encoding());
    if (const auto file = getMachineFile(); file != juce::File())
        xml->setAttribute("machineFile", file.getFullPathName());
    xml->setAttribute("tempoKnob", getTempoKnob());
    copyXmlToBinary(*xml, destData);
}

void RubberDuckProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (!xml || !xml->hasTagName(apvts.state.getType()))
        return;
    juce::MemoryBlock seq;
    const bool hasSequence = seq.fromBase64Encoding(xml->getStringAttribute("sequence"));
    xml->removeAttribute("sequence");
    // A state without a file (older projects, or a new instance's) clears it.
    const auto path = xml->getStringAttribute("machineFile");
    xml->removeAttribute("machineFile");
    setMachineFile(juce::File::isAbsolutePath(path) ? juce::File(path) : juce::File());
    setTempoKnob(xml->getIntAttribute("tempoKnob", kDefaultTempoKnob));
    xml->removeAttribute("tempoKnob");
    // Projects saved before the v1.02 engine existed keep sounding as v2.12.
    const bool hasEngine = xml->getChildByAttribute("id", "engine") != nullptr;
    apvts.replaceState(juce::ValueTree::fromXml(*xml));
    if (!hasEngine)
        setParam(apvts, "engine", static_cast<float>(EngineKind::V212));
    snapSmoothing.store(true);

    const size_t expected = rd::kNumPatterns * rd::kStepsPerPattern * 2 + rd::kSongLength + sizeof(int32_t);
    if (hasSequence && seq.getSize() == expected) {
        SequenceData d;
        const auto* b = static_cast<const uint8_t*>(seq.getData());
        for (auto& pattern : d.patterns)
            for (auto& step : pattern.steps) {
                step.note = static_cast<int8_t>(*b++);
                step.accent = (*b & 1) ? 1 : 0;
                step.slide = (*b & 2) ? 1 : 0;
                ++b;
            }
        std::memcpy(d.song.data(), b, d.song.size());
        b += d.song.size();
        int32_t len;
        std::memcpy(&len, b, sizeof len);
        d.songLength = std::max(1, std::min(len, rd::kSongLength));
        setSequence(d);
    }
}

juce::AudioProcessorEditor* RubberDuckProcessor::createEditor()
{
    return new RubberDuckEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RubberDuckProcessor();
}
