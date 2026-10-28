#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

#include "PluginProcessor.h"

// Edits the steps of one pattern, or of a pair of patterns shown as two rows
// in 32-step mode: note (drag or wheel to change, double click to toggle rest,
// right click for more), accent and slide. Clicking a note puts the keyboard
// cursor there; notes are then typed tracker style (two piano rows at fixed
// key positions, whatever the keyboard layout).
class StepEditor : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit StepEditor(RubberDuckProcessor& p);

    void setPatternIndex(int index);
    int getPatternIndex() const { return patternIndex; }
    int numRows() const;
    // Height needed for `rows` rows of steps.
    static int heightFor(int rows);
    // Re-reads the sequence after an outside change (file load); clears the undo history.
    void reload();

    // Edits of the shown pattern(s), each undoable.
    void shiftSteps(int amount);
    void transpose(int semitones);
    void copy();
    void paste();
    void undo();
    void redo();
    bool canUndo() const { return !undoStack.empty(); }
    bool canRedo() const { return !redoStack.empty(); }
    bool canPaste() const;

    // The pattern(s) on screen, and a name for them ("0", "2+3").
    std::vector<rd::Pattern> shownPatterns() const;
    juce::String shownName() const;

    int getEntryOctave() const { return entryOctave; }
    void setEntryOctave(int octave);
    // Short description of the keys, with the characters of the current layout.
    juce::String keyHelp() const;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType) override;
    void focusLost(FocusChangeType) override { repaint(); }

    static juce::String noteName(int note);

private:
    enum class Row { None, Note, Accent, Slide };
    struct NoteKey {
        juce::juce_wchar character;
        int semitone;  // from C of the entry octave
    };

    void timerCallback() override;
    juce::Rectangle<float> cellBounds(int row, int step, Row kind) const;
    bool hit(juce::Point<float> p, int& row, int& step, Row& kind) const;
    rd::Step& stepAt(int row, int step);
    // Saves the current sequence for undo before an edit.
    void checkpoint();
    void commit();
    void setNote(int row, int step, int note);
    void advanceCursor();
    void moveCursor(int rows, int steps);
    void showStepMenu(int row, int step);
    void buildKeyMap();

    RubberDuckProcessor& processor;
    SequenceData data;
    int patternIndex = 0;
    int lastNote = 48;
    int dragRow = -1, dragStep = -1;
    int dragStartNote = 0;
    float dragStartY = 0;
    bool dragChanged = false;
    int shownStep = -1, shownPattern = -1;
    int shownRows = 1;

    int cursorRow = 0, cursorStep = 0;
    int entryOctave = 4;  // octave of the lower piano row; 4 is "C-4" (MIDI C1)
    std::vector<NoteKey> noteKeys;
    juce::juce_wchar noteOffKey = '1', accentKey = 'a', slideKey = 'f';

    std::vector<SequenceData> undoStack, redoStack;
};
