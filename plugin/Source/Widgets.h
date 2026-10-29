#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

// The custom controls of the editor, drawn with RdLookAndFeel's theme.

// A rotary knob with its name and an editable value under it. The value turns
// acid (dark theme) or ink (light theme) under the mouse; a disabled knob
// (absent from the selected engine) shows a "2.12 ONLY" badge instead.
class RdKnob : public juce::Slider
{
public:
    RdKnob(const juce::String& label, bool big, bool bipolar = false);
    // Column size: 96 x 113 (big) or 64 x 76.
    static juce::Rectangle<int> sizeFor(bool big) { return big ? juce::Rectangle<int>(96, 113) : juce::Rectangle<int>(64, 76); }
    bool isBig() const { return getProperties()["rd.big"]; }

    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void enablementChanged() override;
    void lookAndFeelChanged() override;

private:
    void updateValueBox();
};

// A rectangular key with an LED left of its text (M, Tweak, Tweet, x2).
class LedKey : public juce::Button
{
public:
    explicit LedKey(const juce::String& text, bool compact = false);
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;

private:
    bool compact;
};

// A round pattern key with its LED above.
class PatternKey : public juce::Button
{
public:
    enum class Led { Off, Playing, Pending };
    explicit PatternKey(const juce::String& name);
    void setState(bool selected, Led led);
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;

private:
    bool selected = false;
    Led led = Led::Off;
};

// "STEPS 16 [switch] 32", notched into the sequencer frame; toggles length32.
class StepsSwitch : public juce::Button
{
public:
    StepsSwitch();
    int getIdealWidth() const;
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;
};

// The configuration VFD of the header: segments that each open the menu of a
// choice parameter ("ENG 1.02 · SINGLE · PATTERNS · NEXT BAR").
class VfdSelector : public juce::Component, public juce::TooltipClient
{
public:
    void addSegment(juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& prefix,
                    const juce::StringArray& labels, const juce::String& tooltip);
    int getIdealWidth() const;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    juce::String getTooltip() override;

private:
    struct Segment {
        juce::String prefix, tooltip;
        juce::StringArray labels;
        juce::AudioParameterChoice* param = nullptr;
        std::unique_ptr<juce::ParameterAttachment> attachment;
        int index = 0;
        juce::Rectangle<float> bounds;
    };
    float segmentWidth(const Segment& s) const;
    int segmentAt(juce::Point<float> p) const;

    std::vector<std::unique_ptr<Segment>> segments;
    int hovered = -1;
};

// A choice in a small VFD: "◂ SAW/PULSE ▸". The arrows step through the
// choices (skipping those the engine does not have), the middle opens a menu.
class VfdChoice : public juce::Component, public juce::SettableTooltipClient
{
public:
    VfdChoice(juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::StringArray& labels);
    // Whether choice `index` exists in the selected engine.
    std::function<bool(int)> isChoiceEnabled;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    void select(int index);

    juce::StringArray labels;
    juce::AudioParameterChoice* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    int index = 0;
};

// "0 → 1": the v1.02 sequence range, each number opening its menu.
class RangeVfd : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit RangeVfd(juce::AudioProcessorValueTreeState& state);
    void setActive(bool active);
    bool isActive() const { return active; }

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    juce::AudioParameterChoice* params[2] = {};
    std::unique_ptr<juce::ParameterAttachment> attachments[2];
    int values[2] = {};
    bool active = false;
};

// A small read-only VFD (the entry octave).
class VfdReadout : public juce::Component, public juce::SettableTooltipClient
{
public:
    void setText(const juce::String& newText);
    void paint(juce::Graphics&) override;

private:
    juce::String text;
};

// The pixel duck, straight on the chassis; a click opens the theme menu.
class DuckBadge : public juce::Component, public juce::SettableTooltipClient
{
public:
    DuckBadge();
    std::function<void()> onClick;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
};
