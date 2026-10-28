#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

#include "rd/Engine.h"

// Patterns as a standard MIDI file, written so that the plugin replays it the
// same way in "MIDI notes" mode: pattern note 48 ("C-4") is MIDI 24, accents
// are loud notes, a slide step overlaps the previous note (legato), rests let
// the previous note ring on and a note off ends it.
namespace midiexport {

constexpr int kTicksPerQuarter = 960;
constexpr int kTicksPerStep = kTicksPerQuarter / 4;
constexpr int kAccentVelocity = 120;
constexpr int kNormalVelocity = 80;

// One track: tempo, 4/4, then the steps of `patterns` one after the other.
juce::MidiFile fromPatterns(const std::vector<rd::Pattern>& patterns, double bpm, const juce::String& name);

// Writes the file into the temporary folder (for dragging into a host) and returns it.
juce::File writeTemporary(const juce::MidiFile& midi, const juce::String& name);

} // namespace midiexport
