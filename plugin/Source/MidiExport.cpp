#include "MidiExport.h"

#include "PluginProcessor.h"

namespace midiexport {

static_assert(kAccentVelocity >= RubberDuckProcessor::kAccentVelocity, "accents must replay accented");
static_assert(kNormalVelocity < RubberDuckProcessor::kAccentVelocity, "plain notes must replay plain");

namespace {

// Overlap of a note into the next one when that one slides (legato).
constexpr int kSlideOverlap = kTicksPerStep / 4;

} // namespace

juce::MidiFile fromPatterns(const std::vector<rd::Pattern>& patterns, double bpm, const juce::String& name)
{
    juce::MidiMessageSequence track;
    track.addEvent(juce::MidiMessage::textMetaEvent(3, name), 0);
    track.addEvent(juce::MidiMessage::tempoMetaEvent(static_cast<int>(std::lround(60000000.0 / juce::jmax(1.0, bpm)))), 0);
    track.addEvent(juce::MidiMessage::timeSignatureMetaEvent(4, 4), 0);

    int open = -1;  // MIDI note sounding, -1 if none
    auto close = [&](int tick) {
        if (open >= 0)
            track.addEvent(juce::MidiMessage::noteOff(1, open), tick);
        open = -1;
    };

    int tick = 0;
    for (const auto& pattern : patterns)
        for (const auto& step : pattern.steps) {
            if (step.note == -1) {
                close(tick);
            } else if (step.note >= 0) {
                const int note = juce::jlimit(0, 127, step.note - RubberDuckProcessor::kMidiToPatternNote);
                const auto velocity = static_cast<juce::uint8>(step.accent ? kAccentVelocity : kNormalVelocity);
                if (step.slide && open == note) {
                    // Sliding to the same pitch: the note just carries on.
                } else if (step.slide && open >= 0) {
                    // Note-off after the next note-on: the plugin hears legato and glides.
                    const int previous = open;
                    track.addEvent(juce::MidiMessage::noteOn(1, note, velocity), tick);
                    track.addEvent(juce::MidiMessage::noteOff(1, previous), tick + kSlideOverlap);
                    open = note;
                } else {
                    close(tick);  // added before the note-on at the same tick
                    track.addEvent(juce::MidiMessage::noteOn(1, note, velocity), tick);
                    open = note;
                }
            }
            tick += kTicksPerStep;
        }
    close(tick);
    track.addEvent(juce::MidiMessage::endOfTrack(), tick);
    track.updateMatchedPairs();

    juce::MidiFile file;
    file.setTicksPerQuarterNote(kTicksPerQuarter);
    file.addTrack(track);
    return file;
}

juce::File writeTemporary(const juce::MidiFile& midi, const juce::String& name)
{
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("RubDuck");
    folder.createDirectory();
    const auto file = folder.getChildFile(juce::File::createLegalFileName(name) + ".mid");
    file.deleteFile();
    juce::FileOutputStream out(file);
    if (!out.openedOk() || !midi.writeTo(out))
        return {};
    return file;
}

} // namespace midiexport
