#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

// Palettes, fonts and the theme preference of the "Sérigraphie" interface.
// The dark and light themes share every component: only the colours change.
namespace rd::ui {

enum class ThemeKind { Dark = 0, Light = 1 };

struct Theme {
    ThemeKind kind;
    juce::Colour chassis, ink, label, dim, line, ruler;           // background, text, titles, greyed text, frames, beat ruler
    juce::Colour key, keyHi, keyLo, keyOff, keyOffHi, keyOffLo;   // keys: face, 1 px top edge, 2 px bottom edge
    juce::Colour stepA, stepAHi, stepB, stepBHi, stepLo, stepNum; // step keys, alternating by beat
    juce::Colour vfd, acid, onAcid;                               // VFD windows, acid colour, text on acid
    juce::Colour accOff, ledOffDisabled, slide, slideOff, slideTie, play, playOff, run, runOff, cursor, beak;
    juce::Colour bigCap, bigFace, bigPtr, smallCap, smallStroke, smallPtr, offCap, offStroke, knobShadow, knobShadowOff;
    juce::Colour tickOn, tickOff, tickDisabled, value, valueHover, badgeText, badgeLine;
    juce::Colour patNaCap, patNaHi, patNaText, patSelHi, dashed, switchTrack, switchThumb, switchThumbHi;
    juce::Colour screw, screwRim, screwSlot;
    juce::Colour vfdEdge;  // 1 px light edge under VFD windows
    bool ledRing;          // lit LEDs get a thin dark ring (light theme)

    static const Theme& dark();
    static const Theme& light();
    static const Theme& get(ThemeKind kind) { return kind == ThemeKind::Light ? light() : dark(); }
};

// Global preference (not a parameter, not part of the plugin state), kept in
// "~/Library/Application Support/RubDuck/RubDuck.settings".
ThemeKind preferredTheme();
void setPreferredTheme(ThemeKind kind);

enum class Face { Ui500, Ui600, Ui700, Vfd, Name500, Name700 };

// A font of the embedded families: Barlow Condensed (interface), Share Tech
// Mono (VFD windows) and IBM Plex Mono (the name). `px` is a CSS font size,
// `trackingEm` a CSS letter-spacing in em.
juce::Font font(Face face, float px, float trackingEm = 0.0f);
// Width of `text` once drawn, tracking included.
float textWidth(const juce::Font& f, const juce::String& text);

// Keeps the embedded typefaces registered while an editor is open.
struct Typefaces {
    Typefaces();
    juce::Typeface::Ptr get(Face face) const { return faces[static_cast<size_t>(face)]; }

private:
    std::array<juce::Typeface::Ptr, 6> faces;
};

} // namespace rd::ui
