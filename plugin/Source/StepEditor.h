#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"

// Edits the 16 steps of one pattern: note (drag or wheel to change, double
// click to toggle rest, right click for more), accent and slide.
class StepEditor : public juce::Component, private juce::Timer
{
public:
    explicit StepEditor(RubberDuckProcessor& p);

    void setPatternIndex(int index);
    int getPatternIndex() const { return patternIndex; }
    void reload();

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    static juce::String noteName(int note);

private:
    enum class Row { None, Note, Accent, Slide };

    void timerCallback() override;
    juce::Rectangle<float> cellBounds(int step, Row row) const;
    bool hit(juce::Point<float> p, int& step, Row& row) const;
    void commit();
    void setNote(int step, int note);
    void showStepMenu(int step);

    RubberDuckProcessor& processor;
    SequenceData data;
    int patternIndex = 0;
    int lastNote = 48;
    int dragStep = -1;
    int dragStartNote = 0;
    float dragStartY = 0;
    int shownStep = -1;
};
