#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>
#include <vector>

#include "PluginProcessor.h"
#include "RdLookAndFeel.h"
#include "StepEditor.h"
#include "Widgets.h"

// Dragged out of the window, gives the shown pattern(s) as a MIDI file (drop
// it on a Live track to get a clip); a plain click saves the file instead.
class MidiDragButton : public juce::TextButton
{
public:
    using juce::TextButton::TextButton;
    std::function<juce::File()> makeFile;
    std::function<void()> onPlainClick;

    int getIdealWidth() const;
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void clicked() override;

private:
    bool dragged = false;
};

class RubberDuckEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit RubberDuckEditor(RubberDuckProcessor&);
    ~RubberDuckEditor() override;

    // Shows the editor in `kind`; with `remember`, also makes it the preference
    // that every instance follows.
    void setTheme(rd::ui::ThemeKind kind, bool remember);
    rd::ui::ThemeKind getTheme() const { return themeKind; }

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct Knob {
        Knob(const char* text, bool big, bool bipolar) : slider(text, big, bipolar) {}
        RdKnob slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    struct Toggle {
        Toggle(const juce::String& text, bool compact) : button(text, compact) {}
        LedKey button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };
    struct Caption {
        juce::String text;
        juce::Rectangle<int> bounds;
        bool strong = false;  // ink names (OSC 1) rather than small grey titles
    };

    RdKnob& addKnob(const char* paramId, const char* text, bool big, bool bipolar = false);
    LedKey& addToggle(const char* paramId, const juce::String& text, bool compact = false);
    RdKnob& knob(const char* paramId);
    LedKey& toggle(const char* paramId);
    void timerCallback() override;
    void selectPattern(int index);
    void updatePatternKeys();
    void showThemeMenu();
    void disarmClear();
    void loadFile();
    void saveFile();
    void saveMidi();
    void updateEngineControls();
    bool isV1() const { return processor.getEngineKind() == RubberDuckProcessor::EngineKind::V102; }
    int playMode() const;
    int editorHeight() const;

    RubberDuckProcessor& processor;
    RdLookAndFeel lookAndFeel;
    rd::ui::ThemeKind themeKind = rd::ui::ThemeKind::Dark;
    rd::ui::ThemeKind seenPreference = rd::ui::ThemeKind::Dark;

    std::vector<std::pair<juce::String, std::unique_ptr<Knob>>> knobs;
    std::vector<std::pair<juce::String, std::unique_ptr<Toggle>>> toggles;
    DuckBadge duck;
    VfdSelector config;
    juce::TextButton loadButton{"LOAD"}, saveButton{"SAVE"};
    std::array<std::unique_ptr<PatternKey>, rd::kNumPatterns> patternKeys;
    RangeVfd range;
    MidiDragButton midiButton{"DRAG MIDI"};
    StepsSwitch stepsSwitch;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> stepsAttachment;
    // Sequencer tools, acting on the shown pattern (or pair).
    juce::TextButton shiftLeft, shiftRight;
    juce::TextButton octaveDown{juce::CharPointer_UTF8("\xe2\x88\x92" "12")}, semitoneDown{juce::CharPointer_UTF8("\xe2\x88\x92" "1")};
    juce::TextButton semitoneUp{"+1"}, octaveUp{"+12"};
    juce::TextButton undoButton{"UNDO"}, redoButton{"REDO"}, copyButton{"COPY"}, pasteButton{"PASTE"};
    // CLEAR asks "SURE?" first; a second click within a few seconds, on the
    // same pattern(s), empties them.
    juce::TextButton clearButton{"CLEAR"};
    juce::String clearArmedFor;  // shown pattern(s) when armed, empty when not
    juce::uint32 clearArmedAt = 0;
    juce::TextButton entryOctaveDown{juce::CharPointer_UTF8("\xe2\x88\x92")}, entryOctaveUp{"+"};
    VfdReadout entryOctave;
    VfdChoice osc1, osc2;
    StepEditor stepEditor;
    juce::TooltipWindow tooltipWindow{this, 500};
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::ParameterAttachment> patternAttachment;

    // Laid out by resized(), drawn by paint().
    std::vector<Caption> sections;     // frames with their notched titles
    std::vector<Caption> captions;     // small titles (toolbar groups, OSC 1...)
    std::vector<juce::Rectangle<int>> separators;
    juce::Rectangle<int> rangeCaption;
    int nameX = 0;

    int shownPattern = -1;
    int shownPlaying = -1;
    int shownEngine = -1;
    int shownPlayMode = -1;
    int shownRows = -1;
    int shownRange = -1;
    int pendingPattern = -1;  // selected while another pattern plays, not yet in effect

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RubberDuckEditor)
};
