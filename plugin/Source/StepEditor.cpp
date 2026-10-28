#include "StepEditor.h"

#include <algorithm>

#include "KeyLayout.h"

namespace {

const juce::Colour kBackground(0xff1b1d22);
const juce::Colour kCell(0xff2a2d35);
const juce::Colour kText(0xffe6e6e6);
const juce::Colour kDim(0xff7d828c);
const juce::Colour kAcid(0xffffb000);
const juce::Colour kSlide(0xff4fc3f7);
const juce::Colour kPlay(0xffff5252);

constexpr float kHeaderH = 16.0f;
constexpr float kNoteH = 44.0f;
constexpr float kToggleH = 22.0f;
constexpr float kGap = 4.0f;
constexpr float kRowH = kHeaderH + kNoteH + kToggleH * 2 + kGap * 2;
constexpr float kRowGap = 8.0f;
constexpr int kSteps = rd::kStepsPerPattern;
constexpr size_t kMaxUndo = 200;

// Shared by every editor in the process, so patterns can be copied between instances.
std::vector<rd::Pattern>& clipboard()
{
    static std::vector<rd::Pattern> patterns;
    return patterns;
}

} // namespace

StepEditor::StepEditor(RubberDuckProcessor& p) : processor(p)
{
    setWantsKeyboardFocus(true);
    buildKeyMap();
    reload();
    startTimerHz(30);
}

int StepEditor::numRows() const
{
    return processor.pairMode() ? 2 : 1;
}

int StepEditor::heightFor(int rows)
{
    return static_cast<int>(std::ceil(kRowH * static_cast<float>(rows) + kRowGap * static_cast<float>(rows - 1)));
}

void StepEditor::setPatternIndex(int index)
{
    patternIndex = juce::jlimit(0, rd::kNumPatterns - 1, index);
    repaint();
}

void StepEditor::reload()
{
    data = processor.getSequence();
    undoStack.clear();
    redoStack.clear();
    repaint();
}

void StepEditor::buildKeyMap()
{
    using namespace keylayout;
    // Tracker piano: the lower row from C of the entry octave, the upper row
    // an octave higher, black keys on the row above each.
    static const Position lower[] = {Z, S, X, D, C, V, G, B, N, H, J, M, Comma, L, Period, Semicolon, Slash};
    static const Position upper[] = {Q, K2, W, K3, E, R, K5, T, K6, Y, K7, U, I, K9, O, K0, P};
    noteKeys.clear();
    int semitone = 0;
    for (auto pos : lower)
        noteKeys.push_back({static_cast<juce::juce_wchar>(characterAt(pos)), semitone++});
    semitone = 12;
    for (auto pos : upper)
        noteKeys.push_back({static_cast<juce::juce_wchar>(characterAt(pos)), semitone++});
    noteOffKey = static_cast<juce::juce_wchar>(characterAt(K1));
    accentKey = static_cast<juce::juce_wchar>(characterAt(A));
    slideKey = static_cast<juce::juce_wchar>(characterAt(F));
    setTooltip(keyHelp());
}

juce::String StepEditor::keyHelp() const
{
    auto key = [](juce::juce_wchar c) { return juce::String::charToString(juce::CharacterFunctions::toUpperCase(c)); };
    juce::StringArray lower, upper;
    // noteKeys holds the 17 keys of the lower row, then the 17 of the upper one.
    for (size_t i = 0; i < noteKeys.size(); ++i)
        if (i < 12)
            lower.add(key(noteKeys[i].character));
        else if (i >= 17 && i < 29)
            upper.add(key(noteKeys[i].character));
    return "Click a note, then type: " + lower.joinIntoString(" ") + " (octave " + juce::String(entryOctave) + "), "
           + upper.joinIntoString(" ") + " (octave " + juce::String(entryOctave + 1) + ")\n"
           + key(noteOffKey) + " note off, Backspace rest, " + key(accentKey) + " accent, " + key(slideKey)
           + " slide, arrows move\n"
             "Shift+Left/Right shift the steps, Shift+Up/Down transpose by a semitone, Option+Up/Down by an octave\n"
             "Cmd+Z undo, Cmd+Shift+Z redo, Cmd+C / Cmd+V copy and paste";
}

void StepEditor::setEntryOctave(int octave)
{
    entryOctave = juce::jlimit(0, 7, octave);
    setTooltip(keyHelp());
}

juce::String StepEditor::noteName(int note)
{
    static const char* names[12] = {"C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"};
    if (note == -2)
        return "...";
    if (note == -1)
        return "OFF";
    if (note < 0)
        return "?";
    return juce::String(names[note % 12]) + juce::String(note / 12);
}

juce::Rectangle<float> StepEditor::cellBounds(int row, int step, Row kind) const
{
    const float w = static_cast<float>(getWidth()) / kSteps;
    const float x = w * static_cast<float>(step) + 2.0f;
    const float y = static_cast<float>(row) * (kRowH + kRowGap);
    const float cw = w - 4.0f;
    switch (kind) {
    case Row::Note: return {x, y + kHeaderH, cw, kNoteH};
    case Row::Accent: return {x, y + kHeaderH + kNoteH + kGap, cw, kToggleH};
    case Row::Slide: return {x, y + kHeaderH + kNoteH + kToggleH + 2 * kGap, cw, kToggleH};
    case Row::None: break;
    }
    return {};
}

bool StepEditor::hit(juce::Point<float> p, int& row, int& step, Row& kind) const
{
    for (int r = 0; r < numRows(); ++r)
        for (int s = 0; s < kSteps; ++s)
            for (Row k : {Row::Note, Row::Accent, Row::Slide})
                if (cellBounds(r, s, k).contains(p)) {
                    row = r;
                    step = s;
                    kind = k;
                    return true;
                }
    return false;
}

rd::Step& StepEditor::stepAt(int row, int step)
{
    const int first = numRows() == 2 ? (patternIndex & ~1) : patternIndex;
    return data.patterns[static_cast<size_t>(first + row)].steps[static_cast<size_t>(step)];
}

void StepEditor::paint(juce::Graphics& g)
{
    g.fillAll(kBackground);
    const int rows = numRows();
    const int first = rows == 2 ? (patternIndex & ~1) : patternIndex;
    const bool running = processor.isSequencerRunning();
    const bool focused = hasKeyboardFocus(true);

    for (int r = 0; r < rows; ++r) {
        const int playing = running && processor.getPlayingPattern() == first + r ? processor.getPlayingStep() : -1;
        for (int s = 0; s < kSteps; ++s) {
            const auto& st = stepAt(r, s);
            auto note = cellBounds(r, s, Row::Note);

            g.setColour(s == playing ? kPlay : (s % 4 == 0 ? kDim : kCell));
            g.fillRoundedRectangle(note.getX() + 4, note.getY() - kHeaderH + 4, note.getWidth() - 8, 6, 2);

            g.setColour(kCell);
            g.fillRoundedRectangle(note, 4);
            if (focused && r == cursorRow && s == cursorStep) {
                g.setColour(kText);
                g.drawRoundedRectangle(note.reduced(1.0f), 4, 2.0f);
            }
            g.setColour(st.note >= 0 ? kText : kDim);
            g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
            g.drawText(noteName(st.note), note, juce::Justification::centred);

            for (Row k : {Row::Accent, Row::Slide}) {
                auto b = cellBounds(r, s, k);
                const bool on = k == Row::Accent ? st.accent != 0 : st.slide != 0;
                g.setColour(on ? (k == Row::Accent ? kAcid : kSlide) : kCell);
                g.fillRoundedRectangle(b, 3);
                g.setColour(on ? kBackground : kDim);
                g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
                g.drawText(k == Row::Accent ? "AC" : "SL", b, juce::Justification::centred);
            }
        }
    }
}

void StepEditor::checkpoint()
{
    data = processor.getSequence();
    undoStack.push_back(data);
    if (undoStack.size() > kMaxUndo)
        undoStack.erase(undoStack.begin());
    redoStack.clear();
}

void StepEditor::commit()
{
    processor.setSequence(data);
    repaint();
}

void StepEditor::setNote(int row, int step, int note)
{
    stepAt(row, step).note = static_cast<int8_t>(juce::jlimit(-2, 120, note));
    if (note >= 0)
        lastNote = note;
    commit();
}

void StepEditor::advanceCursor()
{
    moveCursor(0, 1);
}

void StepEditor::moveCursor(int rows, int steps)
{
    const int total = numRows() * kSteps;
    const int index = cursorRow * kSteps + cursorStep + rows * kSteps + steps;
    const int wrapped = ((index % total) + total) % total;
    cursorRow = wrapped / kSteps;
    cursorStep = wrapped % kSteps;
    repaint();
}

void StepEditor::shiftSteps(int amount)
{
    checkpoint();
    const int rows = numRows();
    const int n = rows * kSteps;
    std::vector<rd::Step> old;
    for (int r = 0; r < rows; ++r)
        for (int s = 0; s < kSteps; ++s)
            old.push_back(stepAt(r, s));
    for (int i = 0; i < n; ++i) {
        const int to = ((i + amount) % n + n) % n;
        stepAt(to / kSteps, to % kSteps) = old[static_cast<size_t>(i)];
    }
    commit();
}

void StepEditor::transpose(int semitones)
{
    data = processor.getSequence();
    int low = 127, high = -1;
    for (int r = 0; r < numRows(); ++r)
        for (int s = 0; s < kSteps; ++s)
            if (const int note = stepAt(r, s).note; note >= 0) {
                low = std::min(low, note);
                high = std::max(high, note);
            }
    // Nothing to move, or a note would leave the range: keep the line as it is.
    if (high < 0 || low + semitones < 0 || high + semitones > 120)
        return;
    checkpoint();
    for (int r = 0; r < numRows(); ++r)
        for (int s = 0; s < kSteps; ++s)
            if (auto& st = stepAt(r, s); st.note >= 0)
                st.note = static_cast<int8_t>(st.note + semitones);
    commit();
}

void StepEditor::copy()
{
    data = processor.getSequence();
    auto& clip = clipboard();
    clip.clear();
    const int first = numRows() == 2 ? (patternIndex & ~1) : patternIndex;
    for (int r = 0; r < numRows(); ++r)
        clip.push_back(data.patterns[static_cast<size_t>(first + r)]);
}

bool StepEditor::canPaste() const
{
    return !clipboard().empty();
}

void StepEditor::paste()
{
    const auto& clip = clipboard();
    if (clip.empty())
        return;
    checkpoint();
    // From the cursor's row: a single pattern goes where the cursor is, a pair
    // fills both rows (or the shown row only in 16-step mode).
    const int first = numRows() == 2 ? (patternIndex & ~1) : patternIndex;
    const int startRow = clip.size() == 1 ? cursorRow : 0;
    const int count = std::min(static_cast<int>(clip.size()), numRows() - startRow);
    for (int i = 0; i < count; ++i)
        data.patterns[static_cast<size_t>(first + startRow + i)] = clip[static_cast<size_t>(i)];
    commit();
}

void StepEditor::undo()
{
    if (undoStack.empty())
        return;
    redoStack.push_back(processor.getSequence());
    data = undoStack.back();
    undoStack.pop_back();
    commit();
}

void StepEditor::redo()
{
    if (redoStack.empty())
        return;
    undoStack.push_back(processor.getSequence());
    data = redoStack.back();
    redoStack.pop_back();
    commit();
}

void StepEditor::mouseDown(const juce::MouseEvent& e)
{
    data = processor.getSequence();
    int row, step;
    Row kind;
    if (!hit(e.position, row, step, kind))
        return;
    if (e.mods.isPopupMenu()) {
        showStepMenu(row, step);
        return;
    }
    if (kind == Row::Accent) {
        checkpoint();
        stepAt(row, step).accent = stepAt(row, step).accent ? 0 : 1;
        commit();
    } else if (kind == Row::Slide) {
        checkpoint();
        stepAt(row, step).slide = stepAt(row, step).slide ? 0 : 1;
        commit();
    } else if (kind == Row::Note) {
        cursorRow = row;
        cursorStep = step;
        grabKeyboardFocus();
        dragRow = row;
        dragStep = step;
        dragChanged = false;
        dragStartNote = stepAt(row, step).note >= 0 ? stepAt(row, step).note : lastNote;
        dragStartY = e.position.y;
        repaint();
    }
}

void StepEditor::mouseDrag(const juce::MouseEvent& e)
{
    if (dragStep < 0)
        return;
    const int delta = static_cast<int>((dragStartY - e.position.y) / 6.0f);
    const int note = juce::jlimit(0, 120, dragStartNote + delta);
    if (delta == 0 && stepAt(dragRow, dragStep).note < 0)
        return;
    if (note == stepAt(dragRow, dragStep).note)
        return;
    if (!dragChanged) {
        checkpoint();
        dragChanged = true;
    }
    setNote(dragRow, dragStep, note);
}

void StepEditor::mouseUp(const juce::MouseEvent&)
{
    dragStep = -1;
}

void StepEditor::mouseDoubleClick(const juce::MouseEvent& e)
{
    int row, step;
    Row kind;
    if (!hit(e.position, row, step, kind) || kind != Row::Note)
        return;
    checkpoint();
    setNote(row, step, stepAt(row, step).note >= 0 ? -2 : lastNote);
}

void StepEditor::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    int row, step;
    Row kind;
    if (!hit(e.position, row, step, kind) || kind != Row::Note)
        return;
    checkpoint();
    const int base = stepAt(row, step).note >= 0 ? stepAt(row, step).note : lastNote;
    setNote(row, step, juce::jlimit(0, 120, base + (wheel.deltaY > 0 ? 1 : -1)));
}

bool StepEditor::keyPressed(const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const int code = key.getKeyCode();

    if (mods.isCommandDown()) {
        const auto c = juce::CharacterFunctions::toLowerCase(static_cast<juce::juce_wchar>(code));
        if (c == 'z')
            mods.isShiftDown() ? redo() : undo();
        else if (c == 'c')
            copy();
        else if (c == 'v')
            paste();
        else
            return false;
        return true;
    }

    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey) {
        const int d = code == juce::KeyPress::leftKey ? -1 : 1;
        if (mods.isShiftDown())
            shiftSteps(d);
        else
            moveCursor(0, d);
        return true;
    }
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey) {
        const int d = code == juce::KeyPress::upKey ? 1 : -1;
        if (mods.isShiftDown())
            transpose(d);
        else if (mods.isAltDown())
            transpose(12 * d);
        else
            moveCursor(-d, 0);
        return true;
    }
    if (code == juce::KeyPress::backspaceKey || code == juce::KeyPress::deleteKey) {
        checkpoint();
        setNote(cursorRow, cursorStep, -2);
        advanceCursor();
        return true;
    }
    if (mods.isAnyModifierKeyDown())
        return false;

    const auto c = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
    if (c == 0)
        return false;
    for (const auto& k : noteKeys)
        if (k.character == c) {
            checkpoint();
            setNote(cursorRow, cursorStep, juce::jlimit(0, 120, entryOctave * 12 + k.semitone));
            advanceCursor();
            return true;
        }
    if (c == noteOffKey) {
        checkpoint();
        setNote(cursorRow, cursorStep, -1);
        advanceCursor();
        return true;
    }
    if (c == accentKey || c == slideKey) {
        checkpoint();
        auto& st = stepAt(cursorRow, cursorStep);
        auto& flag = c == accentKey ? st.accent : st.slide;
        flag = flag ? 0 : 1;
        commit();
        return true;
    }
    return false;
}

void StepEditor::focusGained(FocusChangeType)
{
    // The keyboard layout may have changed since the last time.
    buildKeyMap();
    repaint();
}

void StepEditor::showStepMenu(int row, int step)
{
    juce::PopupMenu m;
    m.addItem(1, "Rest");
    m.addItem(2, "Note off");
    m.addSeparator();
    m.addItem(3, "Octave up");
    m.addItem(4, "Octave down");
    m.addSeparator();
    m.addItem(5, "Clear pattern");
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [this, row, step](int r) {
        if (r == 0)
            return;
        checkpoint();
        const int note = stepAt(row, step).note;
        switch (r) {
        case 1: setNote(row, step, -2); break;
        case 2: setNote(row, step, -1); break;
        case 3: if (note >= 0) setNote(row, step, note + 12); break;
        case 4: if (note >= 0) setNote(row, step, note - 12); break;
        case 5: {
            const int first = numRows() == 2 ? (patternIndex & ~1) : patternIndex;
            data.patterns[static_cast<size_t>(first + row)] = rd::Pattern{};
            commit();
            break;
        }
        default: break;
        }
    });
}

void StepEditor::timerCallback()
{
    const int playing = processor.isSequencerRunning() ? processor.getPlayingStep() : -1;
    const int pattern = processor.getPlayingPattern();
    const int rows = numRows();
    if (rows != shownRows) {
        shownRows = rows;
        cursorRow = std::min(cursorRow, rows - 1);
        repaint();
    }
    if (playing != shownStep || pattern != shownPattern) {
        shownStep = playing;
        shownPattern = pattern;
        repaint();
    }
}
