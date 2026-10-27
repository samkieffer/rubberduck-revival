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

    RubberDuckProcessor& processor;
    juce::LookAndFeel_V4 lookAndFeel;

    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<Toggle>> toggles;
    std::vector<std::unique_ptr<Choice>> choices;
    std::array<juce::TextButton, rd::kNumPatterns> patternButtons;
    juce::TextButton loadButton{"Load .h30"}, saveButton{"Save .h30"};
    StepEditor stepEditor;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::ParameterAttachment> patternAttachment;
    std::vector<Section> sections;
    int shownPattern = -1;
    int shownPlaying = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RubberDuckEditor)
};
