#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>
#include <vector>

#include "PluginProcessor.h"
#include "StepEditor.h"

class RubberDuckEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit RubberDuckEditor(RubberDuckProcessor&);
    ~RubberDuckEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct Knob {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    struct Toggle {
        juce::TextButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };
    struct Choice {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    };
    struct Section {
        juce::String title;
        juce::Rectangle<int> bounds;
    };

    Knob& addKnob(const char* paramId, const char* text);
    Toggle& addToggle(const char* paramId, const char* text);
    Choice& addChoice(const char* paramId, const char* text);
    void timerCallback() override;
    void selectPattern(int index);
    void loadFile();
    void saveFile();
    void layoutKnobs(juce::Rectangle<int> area, std::initializer_list<juce::Component*> items);
    void updateEngineControls();
    int editorHeight() const;

    RubberDuckProcessor& processor;
    juce::LookAndFeel_V4 lookAndFeel;

    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<Toggle>> toggles;
    std::vector<std::unique_ptr<Choice>> choices;
    std::array<juce::TextButton, rd::kNumPatterns> patternButtons;
    juce::TextButton loadButton{"Load .h30"}, saveButton{"Save .h30"};
    // Sequencer tools, acting on the shown pattern (or pair).
    juce::TextButton shiftLeft{juce::CharPointer_UTF8("\xe2\x86\x90")}, shiftRight{juce::CharPointer_UTF8("\xe2\x86\x92")};
    juce::TextButton octaveDown{"-12"}, semitoneDown{"-1"}, semitoneUp{"+1"}, octaveUp{"+12"};
    juce::TextButton undoButton{"Undo"}, redoButton{"Redo"}, copyButton{"Copy"}, pasteButton{"Paste"};
    juce::TextButton entryOctaveDown{"-"}, entryOctaveUp{"+"};
    juce::Label entryOctaveLabel;
    StepEditor stepEditor;
    juce::TooltipWindow tooltipWindow{this, 500};
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::ParameterAttachment> patternAttachment;
    std::vector<Section> sections;
    std::vector<Section> captions;  // toolbar group names
    int shownPattern = -1;
    int shownPlaying = -1;
    int shownEngine = -1;
    int shownPlayMode = -1;
    int shownRows = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RubberDuckEditor)
};
