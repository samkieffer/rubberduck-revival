// Headless tests of the plugin processor with a simulated host transport.
//   RubberDuckPluginTests <installer content folder>
#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <cstdio>

#include "../plugin/Source/PluginProcessor.h"
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
        RubberDuckProcessor p;
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
        RubberDuckProcessor p;
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
        RubberDuckProcessor p;
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
        RubberDuckProcessor p;
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
        RubberDuckProcessor a;
        juce::String err;
        a.importH30(acid, err);
        setParam(a, "squelch", 200.0f);
        juce::MemoryBlock state;
        a.getStateInformation(state);
        RubberDuckProcessor b;
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
    }

    // 6. .h30 export then import gives back the same machine state.
    {
        RubberDuckProcessor a;
        juce::String err;
        a.importH30(acid, err);
        auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("rd_roundtrip.h30");
        check(a.exportH30(tmp), "export .h30");
        RubberDuckProcessor b;
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
        RubberDuckProcessor p;
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
        RubberDuckProcessor a;
        juce::String err;
        a.importH30(v1File, err);
        setParam(a, "rangeend", 5.0f);
        setParam(a, "resonance", 201.0f);
        auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("rd_roundtrip_v1.h30");
        check(a.exportH30(tmp), "v1.02: export .h30");
        rd::H30File f;
        check(rd::H30File::load(tmp.getFullPathName().toStdString(), f) && f.version == 2 && f.rangeEnd == 5,
              "v1.02: exported file is version 2 with the range");
        RubberDuckProcessor b;
        b.importH30(tmp, err);
        check(std::lround(b.apvts.getRawParameterValue("resonance")->load()) == 201
                  && b.getEngineKind() == RubberDuckProcessor::EngineKind::V102,
              "v1.02: exported parameters survive the round trip");
        tmp.deleteFile();
    }

    // 9. A project saved before the engine choice existed stays on v2.12.
    {
        RubberDuckProcessor a;
        juce::String err;
        a.importH30(acid, err);
        juce::MemoryBlock state;
        a.getStateInformation(state);
        std::unique_ptr<juce::XmlElement> xml(juce::AudioProcessor::getXmlFromBinary(state.getData(), static_cast<int>(state.getSize())));
        if (auto* e = xml->getChildByAttribute("id", "engine"))
            xml->removeChildElement(e, true);
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary(*xml, old);
        RubberDuckProcessor b;
        check(b.getEngineKind() == RubberDuckProcessor::EngineKind::V102, "new instances start on v1.02");
        b.setStateInformation(old.getData(), static_cast<int>(old.getSize()));
        check(b.getEngineKind() == RubberDuckProcessor::EngineKind::V212, "old projects restore on v2.12");
    }

    // 10. Switching engines while playing, and v1.02 pattern changes at the bar.
    {
        RubberDuckProcessor p;
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
        RubberDuckProcessor p;
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

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
