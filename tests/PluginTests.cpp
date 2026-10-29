// Headless tests of the plugin processor with a simulated host transport.
//   RubberDuckPluginTests <installer content folder>
// With RD_SNAPSHOT_DIR set, the editor tests also save PNG renders of the editor there.
#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <cstdio>

#include "../plugin/Source/KeyLayout.h"
#include "../plugin/Source/MidiExport.h"
#include "../plugin/Source/PluginEditor.h"
#include "../plugin/Source/PluginProcessor.h"
#include "../plugin/Source/StepEditor.h"
#include "rd/EngineV1.h"
#include "rd/Mixing.h"

namespace {

struct FakeHost : juce::AudioPlayHead {
    double bpm = 120, ppq = 0, rate = 44100;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm(bpm);
        p.setPpqPosition(ppq);
        p.setIsPlaying(playing);
        return p;
    }
    void advance(int samples) { ppq += samples / rate * bpm / 60.0; }
};

int failures = 0;
void check(bool ok, const char* what)
{
    std::printf("%-58s %s\n", what, ok ? "OK" : "FAIL");
    if (!ok)
        failures++;
}

void setParam(RubberDuckProcessor& p, const char* id, float value)
{
    auto* param = p.apvts.getParameter(id);
    param->setValueNotifyingHost(param->convertTo0to1(value));
}

// Renders `seconds` of audio through the processor, returns the left channel.
std::vector<float> run(RubberDuckProcessor& p, FakeHost& host, double seconds, int bufferSize,
                       std::function<void(juce::MidiBuffer&, int64_t)> midi = {})
{
    std::vector<float> out;
    juce::AudioBuffer<float> buffer(2, bufferSize);
    juce::MidiBuffer mb;
    const auto total = static_cast<int64_t>(seconds * host.rate);
    for (int64_t t = 0; t < total; t += bufferSize) {
        buffer.clear();
        mb.clear();
        if (midi)
            midi(mb, t);
        p.processBlock(buffer, mb);
        out.insert(out.end(), buffer.getReadPointer(0), buffer.getReadPointer(0) + bufferSize);
        host.advance(bufferSize);
    }
    return out;
}

// A simple line in every pattern (new instances start empty), for the tests
// that need the sequencer to play something.
SequenceData demoSequence()
{
    SequenceData d;
    const int8_t notes[16] = {48, 48, 60, 48, -2, 51, 48, 63, 48, 46, -2, 58, 48, 60, 55, 48};
    const bool accent[16] = {1, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0};
    const bool slide[16] = {0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0};
    for (auto& pattern : d.patterns)
        for (int s = 0; s < 16; ++s) {
            pattern.steps[static_cast<size_t>(s)].note = notes[s];
            pattern.steps[static_cast<size_t>(s)].accent = accent[s];
            pattern.steps[static_cast<size_t>(s)].slide = slide[s];
        }
    return d;
}

struct DemoProcessor : RubberDuckProcessor {
    DemoProcessor() { setSequence(demoSequence()); }
};

double rms(const std::vector<float>& v, size_t from = 0)
{
    double s = 0;
    for (size_t i = from; i < v.size(); ++i)
        s += v[i] * v[i];
    return std::sqrt(s / static_cast<double>(v.size() - from));
}

} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juce;
    const juce::String content = argc > 1 ? argv[1] : "reference/content";
    const juce::File acid(content + "/Acid_was_my_first_love.h30");

    // 1. At 44.1 kHz the plugin must equal the engine driven by host positions.
    {
        DemoProcessor p;
        FakeHost host;
        host.bpm = 127.0;
        p.setPlayHead(&host);
        juce::String err;
        check(p.importH30(acid, err), "import Acid_was_my_first_love.h30");
        p.prepareToPlay(44100, 512);
        auto out = run(p, host, 8.0, 512);

        rd::H30File f;
        rd::H30File::load(acid.getFullPathName().toStdString(), f);
        rd::Engine e;
        f.applyTo(e);
        e.setTempo(127.0f);
        std::vector<int16_t> block(128);
        size_t mismatches = 0;
        long first = -1;
        for (size_t k = 0; k * 128 < out.size(); ++k) {
            const double ppq = static_cast<double>(k * 128) / 44100.0 * 127.0 / 60.0;
            e.setNextPosition(static_cast<uint32_t>(static_cast<int64_t>(std::floor(ppq * 262144.0))));
            if (k == 0) {
                e.forceTrigger();
                e.setRunning(true);
            }
            e.render(block.data(), 1);
            uint32_t g = static_cast<uint32_t>(e.params.volume) << 7;
            rd::mix::applyGain(block.data(), 128, g);
            for (size_t i = 0; i < 128 && k * 128 + i < out.size(); ++i)
                if (std::abs(out[k * 128 + i] - static_cast<float>(block[i]) / 32768.0f) > 0.0f) {
                    if (first < 0)
                        first = static_cast<long>(k * 128 + i);
                    mismatches++;
                }
        }
        if (mismatches)
            std::printf("   %zu mismatching samples, first at %ld\n", mismatches, first);
        check(mismatches == 0, "44.1 kHz output identical to the engine (host sync)");
        check(rms(out) > 0.01, "44.1 kHz output is not silent");
    }

    // 2. 48 kHz: resampled, same loudness, latency reported, no NaN.
    {
        DemoProcessor p;
        FakeHost host;
        host.rate = 48000;
        host.bpm = 127.0;
        p.setPlayHead(&host);
        juce::String err;
        p.importH30(acid, err);
        p.prepareToPlay(48000, 480);
        auto out = run(p, host, 8.0, 480);
        bool finite = true;
        for (float x : out)
            finite &= std::isfinite(x);
        check(finite, "48 kHz output finite");
        check(rms(out) > 0.01, "48 kHz output is not silent");
        check(p.getLatencySamples() > 0, "48 kHz reports resampler latency");
    }

    // 3. Pattern change requested mid-bar waits for the next bar.
    {
        DemoProcessor p;
        FakeHost host;
        p.setPlayHead(&host);
        p.prepareToPlay(44100, 256);
        run(p, host, 0.5, 256);  // half a bar at 120 BPM
        setParam(p, "pattern", 3.0f);
        run(p, host, 0.4, 256);
        check(p.getPlayingPattern() == 0, "pattern change waits for the bar line");
        run(p, host, 1.5, 256);
        check(p.getPlayingPattern() == 2, "pattern change applied at the next bar");
    }

    // 4. MIDI notes drive the voice when the source is MIDI.
    {
        DemoProcessor p;
        FakeHost host;
        host.playing = false;
        p.setPlayHead(&host);
        setParam(p, "source", 1.0f);
        p.prepareToPlay(44100, 256);
        auto silent = run(p, host, 0.3, 256);
        auto played = run(p, host, 1.0, 256, [](juce::MidiBuffer& mb, int64_t t) {
            if (t == 0)
                mb.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(110)), 0);
            if (t == 256 * 40)
                mb.addEvent(juce::MidiMessage::noteOn(1, 43, static_cast<juce::uint8>(80)), 0);  // legato: slide
            if (t == 256 * 80) {
                mb.addEvent(juce::MidiMessage::noteOff(1, 36), 0);
                mb.addEvent(juce::MidiMessage::noteOff(1, 43), 1);
            }
        });
        check(rms(silent) < 1e-6, "MIDI mode is silent without notes");
        check(rms(played) > 0.01, "MIDI notes produce sound");
    }

    // 5. State round trip keeps parameters and patterns.
    {
        DemoProcessor a;
        juce::String err;
        a.importH30(acid, err);
        setParam(a, "squelch", 200.0f);
        juce::MemoryBlock state;
        a.getStateInformation(state);
        DemoProcessor b;
        b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        const auto sa = a.getSequence(), sb = b.getSequence();
        bool same = sa.songLength == sb.songLength && sa.song == sb.song;
        for (int i = 0; i < rd::kNumPatterns; ++i)
            for (int s = 0; s < rd::kStepsPerPattern; ++s) {
                const auto& x = sa.patterns[static_cast<size_t>(i)].steps[static_cast<size_t>(s)];
                const auto& y = sb.patterns[static_cast<size_t>(i)].steps[static_cast<size_t>(s)];
                same &= x.note == y.note && (x.accent != 0) == (y.accent != 0) && (x.slide != 0) == (y.slide != 0);
            }
        check(same, "state round trip keeps patterns and song");
        check(std::lround(b.apvts.getRawParameterValue("squelch")->load()) == 200, "state round trip keeps parameters");

        // The machine state file goes with the state; a state without one clears it.
        const juce::File machine("/Users/test/Patterns/Acid line.h30");
        a.setMachineFile(machine);
        a.getStateInformation(state);
        b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        const bool kept = b.getMachineFile() == machine;
        DemoProcessor fresh;
        check(fresh.getMachineFile() == juce::File(), "new instances have no machine state file");
        fresh.getStateInformation(state);
        b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        check(kept && b.getMachineFile() == juce::File(), "state round trip keeps the machine state file");
    }

    // 6. .h30 export then import gives back the same machine state.
    {
        DemoProcessor a;
        juce::String err;
        a.importH30(acid, err);
        auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("rd_roundtrip.h30");
        check(a.exportH30(tmp), "export .h30");
        DemoProcessor b;
        check(b.importH30(tmp, err), "re-import exported .h30");
        check(std::lround(b.apvts.getRawParameterValue("cutoff")->load())
                  == std::lround(a.apvts.getRawParameterValue("cutoff")->load()),
              "exported parameters survive the round trip");
        tmp.deleteFile();
    }

    // 7. v1.02 file: selects the 1997 engine, output identical to EngineV1
    //    driven by the host positions.
    const juce::File v1File(content + "/80'$$$.h30");
    {
        DemoProcessor p;
        FakeHost host;
        host.bpm = 133.0;
        p.setPlayHead(&host);
        juce::String err;
        check(p.importH30(v1File, err), "import 80'$$$.h30 (v1.02 file)");
        check(p.getEngineKind() == RubberDuckProcessor::EngineKind::V102, "v1.02 file selects the v1.02 engine");
        p.prepareToPlay(44100, 441);
        auto out = run(p, host, 8.0, 441);

        rd::H30File f;
        rd::H30File::load(v1File.getFullPathName().toStdString(), f);
        rd::EngineV1 e;
        f.applyTo(e);
        e.setTempo(133.0f);
        std::vector<int16_t> block(64);
        size_t mismatches = 0;
        for (size_t k = 0; k * 64 < out.size(); ++k) {
            const double ppq = static_cast<double>(k * 64) / 44100.0 * 133.0 / 60.0;
            if (k == 0) {
                e.forceTrigger();
                e.setRunning(true);
            }
            e.setNextPosition(static_cast<uint32_t>(static_cast<int64_t>(std::floor(ppq * 262144.0))));
            e.render(block.data(), 1);
            rd::EngineV1::applyVolume(block.data(), 64, e.params.volume);
            for (size_t i = 0; i < 64 && k * 64 + i < out.size(); ++i)
                mismatches += out[k * 64 + i] != static_cast<float>(block[i]) / 32768.0f;
        }
        if (mismatches)
            std::printf("   %zu mismatching samples\n", mismatches);
        check(mismatches == 0, "v1.02: 44.1 kHz output identical to EngineV1 (host sync)");
        check(rms(out) > 0.01, "v1.02: output is not silent");
    }

    // 8. v1.02 export writes a version-2 file that loads back the same.
    {
        DemoProcessor a;
        juce::String err;
        a.importH30(v1File, err);
        setParam(a, "rangeend", 5.0f);
        setParam(a, "resonance", 201.0f);
        auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("rd_roundtrip_v1.h30");
        check(a.exportH30(tmp), "v1.02: export .h30");
        rd::H30File f;
        check(rd::H30File::load(tmp.getFullPathName().toStdString(), f) && f.version == 2 && f.rangeEnd == 5,
              "v1.02: exported file is version 2 with the range");
        DemoProcessor b;
        b.importH30(tmp, err);
        check(std::lround(b.apvts.getRawParameterValue("resonance")->load()) == 201
                  && b.getEngineKind() == RubberDuckProcessor::EngineKind::V102,
              "v1.02: exported parameters survive the round trip");
        tmp.deleteFile();
    }

    // 9. A project saved before the engine choice existed stays on v2.12.
    {
        DemoProcessor a;
        juce::String err;
        a.importH30(acid, err);
        juce::MemoryBlock state;
        a.getStateInformation(state);
        std::unique_ptr<juce::XmlElement> xml(juce::AudioProcessor::getXmlFromBinary(state.getData(), static_cast<int>(state.getSize())));
        if (auto* e = xml->getChildByAttribute("id", "engine"))
            xml->removeChildElement(e, true);
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary(*xml, old);
        DemoProcessor b;
        check(b.getEngineKind() == RubberDuckProcessor::EngineKind::V102, "new instances start on v1.02");
        b.setStateInformation(old.getData(), static_cast<int>(old.getSize()));
        check(b.getEngineKind() == RubberDuckProcessor::EngineKind::V212, "old projects restore on v2.12");
    }

    // 10. Switching engines while playing, and v1.02 pattern changes at the bar.
    {
        DemoProcessor p;
        FakeHost host;
        p.setPlayHead(&host);
        p.prepareToPlay(44100, 256);
        auto a = run(p, host, 1.0, 256);
        setParam(p, "engine", 1.0f);
        auto b = run(p, host, 1.0, 256);
        setParam(p, "engine", 0.0f);
        auto c = run(p, host, 0.5, 256);
        check(rms(a) > 0.01 && rms(b) > 0.01 && rms(c) > 0.01, "engine switch while playing keeps playing");
        setParam(p, "pattern", 4.0f);
        run(p, host, 0.4, 256);
        check(p.getPlayingPattern() != 3, "v1.02: pattern change waits for the bar line");
        run(p, host, 2.1, 256);
        check(p.getPlayingPattern() == 3, "v1.02: pattern change applied at the next bar");
        setParam(p, "pattern", 16.0f);
        run(p, host, 2.1, 256);
        check(p.getPlayingPattern() == 3, "v1.02: patterns past 14 are ignored");
    }

    // 11. v1.02 live notes from the very first block.
    {
        DemoProcessor p;
        FakeHost host;
        host.playing = false;
        p.setPlayHead(&host);
        setParam(p, "source", 1.0f);
        p.prepareToPlay(44100, 128);
        auto played = run(p, host, 0.5, 128, [](juce::MidiBuffer& mb, int64_t t) {
            if (t == 0)
                mb.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(120)), 0);
        });
        check(rms(played) > 0.01, "v1.02: MIDI note on the first block plays");
    }

    // 12. Loading a file mid-bar switches to its pattern at once (no steps of
    //     the new file played at the previous pattern index).
    for (const float engineKind : {0.0f, 1.0f}) {
        DemoProcessor source;
        setParam(source, "engine", engineKind);
        const auto tmp = juce::File::createTempFile("h30");
        source.exportH30(tmp);

        DemoProcessor p;
        FakeHost host;
        p.setPlayHead(&host);
        setParam(p, "engine", engineKind);
        setParam(p, "pattern", 6.0f);
        p.prepareToPlay(44100, 256);
        run(p, host, 4.5, 256);
        const bool before = p.getPlayingPattern() == 5;
        juce::String err;
        p.importH30(tmp, err);
        run(p, host, 0.1, 256);
        check(before && p.getPlayingPattern() == 0,
              engineKind == 0.0f ? "v1.02: loading a file switches pattern at once" : "loading a file switches pattern at once");
        tmp.deleteFile();
    }

    // 13. v1.02 Sequence mode: a pattern chosen for the next bar is the one
    //     played there, then the range carries on from it.
    {
        DemoProcessor p;
        FakeHost host;
        p.setPlayHead(&host);
        setParam(p, "playmode", 1.0f);
        setParam(p, "rangeend", 7.0f);
        p.prepareToPlay(44100, 256);
        run(p, host, 1.0, 256);
        setParam(p, "pattern", 6.0f);
        run(p, host, 1.5, 256);
        const bool chosen = p.getPlayingPattern() == 5;
        run(p, host, 2.0, 256);
        check(chosen && p.getPlayingPattern() == 6, "v1.02 Sequence: next-bar change plays the chosen pattern");
    }

    // 14. 32-step mode: the selected pair alternates bar by bar, and sounds
    //     exactly like the engine's own sequence of the same two patterns.
    for (const float engineKind : {0.0f, 1.0f}) {
        auto setup = [engineKind](RubberDuckProcessor& p, FakeHost& host) {
            p.setPlayHead(&host);
            setParam(p, "engine", engineKind);
            SequenceData d = p.getSequence();
            for (auto& st : d.patterns[1].steps)
                if (st.note >= 0)
                    st.note = static_cast<int8_t>(st.note + 5);
            d.patterns[1].steps[3].accent = 1;
            d.song[0] = 0;
            d.song[1] = 1;
            d.songLength = 2;
            p.setSequence(d);
            p.prepareToPlay(44100, 256);
        };
        DemoProcessor pairs, sequence;
        FakeHost hostA, hostB;
        setup(pairs, hostA);
        setup(sequence, hostB);
        setParam(pairs, "length32", 1.0f);
        setParam(sequence, "playmode", 1.0f);
        setParam(sequence, "rangeend", 1.0f);

        run(pairs, hostA, 1.0, 256);
        const bool first = pairs.getPlayingPattern() == 0;
        run(pairs, hostA, 2.0, 256);
        const bool second = pairs.getPlayingPattern() == 1;
        run(pairs, hostA, 2.0, 256);
        check(first && second && pairs.getPlayingPattern() == 0,
              engineKind == 0.0f ? "v1.02: 32-step pair alternates each bar" : "32-step pair alternates each bar");

        DemoProcessor pairs2;
        FakeHost hostC;
        setup(pairs2, hostC);
        setParam(pairs2, "length32", 1.0f);
        const auto a = run(pairs2, hostC, 9.0, 256);
        const auto b = run(sequence, hostB, 9.0, 256);
        check(a == b && rms(a) > 0.01, engineKind == 0.0f ? "v1.02: 32-step pair sounds like Sequence 0..1"
                                                          : "32-step pair sounds like the song 0, 1");
    }

    // 15. Editor: tracker-style note entry, shift, transpose, undo, 32-step rows.
    {
        DemoProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
        // Renders do not depend on the theme preference of this machine.
        if (auto* e = dynamic_cast<RubberDuckEditor*>(editor.get()))
            e->setTheme(rd::ui::ThemeKind::Dark, false);
        StepEditor* steps = nullptr;
        for (auto* c : editor->getChildren())
            if (auto* s = dynamic_cast<StepEditor*>(c))
                steps = s;
        const auto snapshotDir = juce::SystemStats::getEnvironmentVariable("RD_SNAPSHOT_DIR", {});
        auto settle = [&](const char* name) {
            juce::MessageManager::getInstance()->runDispatchLoopUntil(150);
            if (snapshotDir.isEmpty())
                return;
            const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
            const auto file = juce::File(snapshotDir).getChildFile(name);
            file.deleteFile();
            juce::FileOutputStream os(file);
            juce::PNGImageFormat().writeImageToStream(image, os);
        };
        auto type = [&](keylayout::Position position) {
            const auto c = static_cast<juce::juce_wchar>(keylayout::characterAt(position));
            steps->keyPressed(juce::KeyPress(static_cast<int>(c), {}, c));
        };
        auto key = [&](int code, juce::ModifierKeys mods = {}) { steps->keyPressed(juce::KeyPress(code, mods, 0)); };
        auto note = [&](int pattern, int step) { return p.getSequence().patterns[static_cast<size_t>(pattern)].steps[static_cast<size_t>(step)]; };
        settle("1-default.png");

        // Lower row Z X C = C, D, E of the entry octave (4 -> 48), upper row Q = C an octave up.
        type(keylayout::Z);
        type(keylayout::X);
        type(keylayout::Q);
        type(keylayout::K1);
        key(juce::KeyPress::leftKey);
        type(keylayout::A);
        check(note(0, 0).note == 48 && note(0, 1).note == 50 && note(0, 2).note == 60 && note(0, 3).note == -1
                  && note(0, 3).accent == 1,
              "keyboard entry: notes, note off and accent");
        key(juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier);
        check(note(0, 0).note == 49 && note(0, 2).note == 61, "transpose a semitone up");
        key(juce::KeyPress::rightKey, juce::ModifierKeys::shiftModifier);
        check(note(0, 1).note == 49 && note(0, 0).note == 49 /* old step 16, transposed */
                  && note(0, 4).accent == 1,
              "shift the steps right");
        key('z', juce::ModifierKeys::commandModifier);
        key('z', juce::ModifierKeys::commandModifier);
        check(note(0, 0).note == 48 && note(0, 1).note == 50, "undo twice");
        key('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
        check(note(0, 0).note == 49, "redo");
        settle("2-typed.png");

        setParam(p, "length32", 1.0f);
        setParam(p, "engine", 1.0f);
        settle("3-pairs-v212.png");
        check(steps->numRows() == 2 && editor->getHeight() > 640, "32-step mode shows two rows");
        setParam(p, "engine", 0.0f);
        setParam(p, "playmode", 1.0f);
        settle("4-pairs-v102-sequence.png");

        // CLEAR asks first, then empties the shown pair; undo brings it back.
        juce::TextButton* clear = nullptr;
        for (auto* c : editor->getChildren())
            if (auto* b = dynamic_cast<juce::TextButton*>(c); b != nullptr && b->getButtonText() == "CLEAR")
                clear = b;
        auto isEmpty = [&](int pattern) {
            const auto sequence = p.getSequence();
            for (const auto& st : sequence.patterns[static_cast<size_t>(pattern)].steps)
                if (st.note != -2 || st.accent != 0 || st.slide != 0)
                    return false;
            return true;
        };
        const bool found = clear != nullptr && clear->isEnabled();
        if (found)
            clear->triggerClick();
        settle("5-clear-armed.png");
        const bool armed = found && clear->getButtonText() == "SURE?" && !isEmpty(0);
        if (found)
            clear->triggerClick();
        settle("6-cleared.png");
        check(armed && isEmpty(0) && isEmpty(1) && !isEmpty(2) && clear->getButtonText() == "CLEAR",
              "CLEAR: asks once, then empties the shown pair");
        key('z', juce::ModifierKeys::commandModifier);
        check(note(0, 0).note == 49 && !isEmpty(1), "CLEAR: undo brings the patterns back");

        // Tooltips have rounded corners: the editor must show through them, also
        // when only the tip's area is repainted (as when it appears).
        juce::TooltipWindow* tips = nullptr;
        for (auto* c : editor->getChildren())
            if (auto* t = dynamic_cast<juce::TooltipWindow*>(c))
                tips = t;
        bool cornersOk = false;
        if (tips != nullptr) {
            tips->displayTip(editor->localPointToGlobal(juce::Point<int>(300, 140)), "Tooltip corners");
            const auto area = tips->getBounds();
            const auto withTip = editor->createComponentSnapshot(area);
            tips->hideTip();
            const auto plain = editor->createComponentSnapshot(area);
            cornersOk = withTip.getPixelAt(0, 0) == plain.getPixelAt(0, 0)
                        && withTip.getPixelAt(8, 8) == rd::ui::Theme::dark().key;
        }
        check(cornersOk, "tooltip corners show the editor");
    }

    // 15c. A new instance starts with empty patterns.
    {
        RubberDuckProcessor p;
        bool empty = true;
        const auto sequence = p.getSequence();
        for (const auto& pattern : sequence.patterns)
            for (const auto& st : pattern.steps)
                empty = empty && st.note == -2 && st.accent == 0 && st.slide == 0;
        check(empty, "new instances start with empty patterns");
    }

    // 15b. Editor renders (with RD_SNAPSHOT_DIR): both themes, 16 and 32 steps,
    //      v1.02 Single, v1.02 Sequence (range 0..1) and v2.12, playing step 6 of
    //      pattern 0 with the keyboard cursor on step 10, as on the design boards.
    if (const auto dir = juce::SystemStats::getEnvironmentVariable("RD_SNAPSHOT_DIR", {}); dir.isNotEmpty()) {
        // The line of the boards: notes (49 = C#4, -1 note off, -2 rest), accents, slides.
        const int line[16] = {49, 51, 61, -1, -2, 52, 49, 64, 49, 47, -2, 59, 49, 61, 56, 49};
        auto boardSequence = [&](const SequenceData& base) {
            auto seq = base;
            for (int pattern = 0; pattern < 2; ++pattern)
                for (int s = 0; s < 16; ++s) {
                    auto& st = seq.patterns[static_cast<size_t>(pattern)].steps[static_cast<size_t>(s)];
                    st.note = static_cast<int8_t>(pattern == 0 || line[s] < 0 ? line[s] : line[s] - 2);
                    st.accent = s == 0 || s == 3 || s == 5 || s == 8 || s == 11 || s == 14 ? 1 : 0;
                    st.slide = s == 2 || s == 7 || s == 12 || (pattern == 0 && s == 15) ? 1 : 0;
                }
            return seq;
        };
        struct State { const char* name; float engine, playMode; };
        const State states[] = {{"v102-single", 0.0f, 0.0f}, {"v102-sequence", 0.0f, 1.0f}, {"v212", 1.0f, 0.0f}};
        int written = 0;
        for (const bool pairs : {false, true})
            for (const auto& state : states) {
                DemoProcessor r;
                FakeHost host;
                r.setPlayHead(&host);
                setParam(r, "engine", state.engine);
                setParam(r, "playmode", state.playMode);
                setParam(r, "rangestart", 0.0f);
                setParam(r, "rangeend", 1.0f);
                setParam(r, "length32", pairs ? 1.0f : 0.0f);
                r.setSequence(boardSequence(r.getSequence()));
                // A long name in the Sequence renders, to see it cut.
                r.setMachineFile(juce::File(state.playMode == 1.0f
                                                ? "/Users/test/Patterns/Rubber Duck session - night bass line with slides and accents, take 3.h30"
                                                : "/Users/test/Patterns/Acid_was_my_first_love.h30"));
                r.prepareToPlay(44100, 128);
                for (int i = 0; i < 2000 && !(r.getPlayingStep() == 5 && r.getPlayingPattern() == 0); ++i)
                    run(r, host, 128.0 / 44100.0, 128);
                std::unique_ptr<juce::AudioProcessorEditor> view(r.createEditor());
                auto* editorView = dynamic_cast<RubberDuckEditor*>(view.get());
                StepEditor* grid = nullptr;
                for (auto* c : view->getChildren())
                    if (auto* s = dynamic_cast<StepEditor*>(c))
                        grid = s;
                grid->getProperties().set("rd.showCursor", true);
                for (int i = 0; i < 9; ++i)
                    grid->keyPressed(juce::KeyPress(juce::KeyPress::rightKey, {}, 0));
                for (const auto theme : {rd::ui::ThemeKind::Dark, rd::ui::ThemeKind::Light}) {
                    editorView->setTheme(theme, false);
                    juce::MessageManager::getInstance()->runDispatchLoopUntil(120);
                    const auto image = view->createComponentSnapshot(view->getLocalBounds(), true, 2.0f);
                    const auto file = juce::File(dir).getChildFile(
                        juce::String(theme == rd::ui::ThemeKind::Dark ? "dark" : "light") + (pairs ? "-32-" : "-16-")
                        + state.name + ".png");
                    file.deleteFile();
                    juce::FileOutputStream os(file);
                    if (juce::PNGImageFormat().writeImageToStream(image, os))
                        written++;
                }
            }
        check(written == 12, "editor renders: 2 themes x 16/32 steps x 3 engine states");
    }

    // 16. MIDI export of the demo line: pitches as replayed in MIDI mode,
    //     accents loud, slides legato, rests ring on, one bar per pattern.
    {
        DemoProcessor p;
        const auto patterns = std::vector<rd::Pattern>{p.getSequence().patterns[0], p.getSequence().patterns[1]};
        const auto midi = midiexport::fromPatterns(patterns, 127.0, "test");
        const auto& track = *midi.getTrack(0);
        struct Note { int pitch, velocity; double on, off; };
        std::vector<Note> notes;
        for (int i = 0; i < track.getNumEvents(); ++i)
            if (const auto& m = track.getEventPointer(i)->message; m.isNoteOn())
                notes.push_back({m.getNoteNumber(), m.getVelocity(), m.getTimeStamp(), track.getTimeOfMatchingKeyUp(i)});
        constexpr int T = midiexport::kTicksPerStep;
        check(notes.size() == 28 && notes[0].pitch == 24 && notes[0].velocity >= RubberDuckProcessor::kAccentVelocity
                  && notes[1].velocity < RubberDuckProcessor::kAccentVelocity,
              "MIDI export: notes, pitches and accents");
        check(notes[2].pitch == 36 && notes[2].on == 2 * T && notes[1].off > notes[2].on, "MIDI export: slide overlaps");
        check(notes[3].on == 3 * T && notes[2].off == 3 * T && notes[3].off == 5 * T, "MIDI export: rests ring on");
        check(notes.back().off == 32 * T && track.getEndTime() == 32 * T, "MIDI export: two patterns make two bars");

        // Replayed through the plugin in MIDI mode, the clip gives back the sequencer's line.
        for (const float engineKind : {0.0f, 1.0f}) {
            auto render = [&](bool fromMidi) {
                DemoProcessor r;
                FakeHost host;
                r.setPlayHead(&host);
                setParam(r, "engine", engineKind);
                if (fromMidi)
                    setParam(r, "source", 1.0f);
                r.prepareToPlay(44100, 128);
                const double samplesPerTick = 44100.0 * 60.0 / host.bpm / midiexport::kTicksPerQuarter;
                return run(r, host, 4.0, 128, [&](juce::MidiBuffer& mb, int64_t t) {
                    if (!fromMidi)
                        return;
                    for (int i = 0; i < track.getNumEvents(); ++i) {
                        const auto& m = track.getEventPointer(i)->message;
                        const auto at = static_cast<int64_t>(std::llround(m.getTimeStamp() * samplesPerTick));
                        if ((m.isNoteOn() || m.isNoteOff()) && at >= t && at < t + 128)
                            mb.addEvent(m, static_cast<int>(at - t));
                    }
                });
            };
            const auto seq = render(false), clip = render(true);
            double num = 0, den = 0;
            for (size_t i = 0; i < seq.size(); ++i) {
                num += (seq[i] - clip[i]) * (seq[i] - clip[i]);
                den += seq[i] * seq[i];
            }
            // v2.12 replays sample for sample; v1.02 within -60 dB (live notes and
            // the sequencer differ in tiny internal details there).
            check(engineKind == 1.0f ? num == 0.0 : num / den < 1e-6, engineKind == 0.0f ? "v1.02: exported clip replays the line" : "exported clip replays the line");
        }
    }

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
