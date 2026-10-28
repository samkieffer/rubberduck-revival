#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>

#include "Theme.h"

// Draws the plugin's controls in the current theme, and offers the shared
// pieces (keys, VFD windows, LEDs, glowing text) to the custom components.
//
// Component properties read here:
//   Slider      "rd.label" (text under the knob), "rd.big" (80 px knob instead
//               of 46 px), "rd.bipolar" (ticks lit from the centre)
//   TextButton  "rd.fontSize", "rd.weight" (500/600/700), "rd.tracking" (em),
//               "rd.radius", "rd.icon" ("left" / "right": a triangle instead of text)
class RdLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RdLookAndFeel();

    void setTheme(const rd::ui::Theme& theme);
    const rd::ui::Theme& theme() const { return *current; }
    // The look and feel of `c` if it is ours.
    static RdLookAndFeel* of(juce::Component& c) { return dynamic_cast<RdLookAndFeel*>(&c.getLookAndFeel()); }

    // A key: face, 1 px light top edge, 2 px dark bottom edge (swapped when down).
    void drawKey(juce::Graphics& g, juce::Rectangle<float> r, float radius, bool enabled, bool hover, bool down) const;
    void drawKey(juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour face, juce::Colour hi,
                 juce::Colour lo, bool down = false) const;
    // A VFD window: dark glass with an inner shadow.
    void drawVfdWindow(juce::Graphics& g, juce::Rectangle<float> r, float radius) const;
    // Text with an optional glow of its own colour (VFD text).
    void drawGlowText(juce::Graphics& g, const juce::String& text, const juce::Font& font, juce::Rectangle<float> area,
                      juce::Colour colour, juce::Justification justification, bool glow);
    // A round LED; lit ones glow, and get a thin dark ring with `ring` when the theme asks for it.
    void drawLed(juce::Graphics& g, juce::Point<float> centre, float diameter, juce::Colour colour, bool lit,
                 bool ring = false) const;
    // A small filled triangle pointing left or right.
    static void drawTriangle(juce::Graphics& g, juce::Rectangle<float> r, bool right, juce::Colour colour);

    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float sliderPos, float startAngle,
                          float endAngle, juce::Slider&) override;
    juce::Slider::SliderLayout getSliderLayout(juce::Slider&) override;
    juce::Label* createSliderTextBox(juce::Slider&) override;

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;

    void drawPopupMenuBackground(juce::Graphics&, int width, int height) override;
    juce::Font getPopupMenuFont() override;

    juce::Rectangle<int> getTooltipBounds(const juce::String& tipText, juce::Point<int> screenPos,
                                          juce::Rectangle<int> parentArea) override;
    void drawTooltip(juce::Graphics&, const juce::String& text, int width, int height) override;

private:
    juce::TextLayout tooltipLayout(const juce::String& text) const;

    juce::SharedResourcePointer<rd::ui::Typefaces> typefaces;
    const rd::ui::Theme* current = &rd::ui::Theme::dark();
    std::map<juce::String, juce::Image> glowCache;  // blurred glyphs, by text, size and colour
};
