#include "StepEditor.h"

#include <algorithm>

#include "KeyLayout.h"
#include "RdLookAndFeel.h"

namespace {

// Geometry of a row of 16 steps, from the top: beat ruler, NOTE, ACCENT, SLIDE, STEP.
constexpr float kRulerH = 14.0f;
constexpr float kNoteH = 30.0f;
constexpr float kLedH = 12.0f;
constexpr float kKeyH = 30.0f;
constexpr float kLineGap = 6.0f;
constexpr float kNoteY = kRulerH + kLineGap;
constexpr float kAccentY = kNoteY + kNoteH + kLineGap;
constexpr float kSlideY = kAccentY + kLedH + kLineGap;
constexpr float kKeyY = kSlideY + kLedH + kLineGap;
constexpr float kRowH = kKeyY + kKeyH;  // 122
constexpr float kRowGap = 14.0f;
constexpr float kLabelW = 58.0f;  // label column, then 12 px
constexpr float kGroupGap = 12.0f;
constexpr float kCellGap = 4.0f;
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
        return "---";
    if (note == -1)
        return "OFF";
    if (note < 0)
        return "?";
    return juce::String(names[note % 12]) + juce::String(note / 12);
}

float StepEditor::groupWidth() const
{
    return (static_cast<float>(getWidth()) - kLabelW - kGroupGap - 3 * kGroupGap) / 4.0f;
}

float StepEditor::cellWidth() const
{
    return (groupWidth() - 3 * kCellGap) / 4.0f;
}

float StepEditor::cellX(int step) const
{
    const int group = step / 4, inGroup = step % 4;
    return kLabelW + kGroupGap + static_cast<float>(group) * (groupWidth() + kGroupGap)
           + static_cast<float>(inGroup) * (cellWidth() + kCellGap);
}

juce::Rectangle<float> StepEditor::cellBounds(int row, int step, Row kind) const
{
    const float x = cellX(step), w = cellWidth();
    const float y = static_cast<float>(row) * (kRowH + kRowGap);
    switch (kind) {
    case Row::Note: return {x, y + kNoteY, w, kNoteH};
    case Row::Accent: return {x, y + kAccentY, w, kLedH};
    case Row::Slide: return {x, y + kSlideY, w, kLedH};
    case Row::Step: return {x, y + kKeyY, w, kKeyH};
    case Row::None: break;
    }
    return {};
}

bool StepEditor::hit(juce::Point<float> p, int& row, int& step, Row& kind) const
{
    for (int r = 0; r < numRows(); ++r)
        for (int s = 0; s < kSteps; ++s)
            for (Row k : {Row::Note, Row::Accent, Row::Slide, Row::Step}) {
                // The LED rows take the whole step width and half the gaps around them.
                const auto zone = k == Row::Accent || k == Row::Slide ? cellBounds(r, s, k).expanded(0.0f, kLineGap / 2)
                                                                      : cellBounds(r, s, k);
                if (zone.contains(p)) {
                    row = r;
                    step = s;
                    kind = k;
                    return true;
                }
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
    auto* lf = RdLookAndFeel::of(*this);
    if (lf == nullptr)
        return;
    const auto& t = lf->theme();
    const int rows = numRows();
    const int first = rows == 2 ? (patternIndex & ~1) : patternIndex;
    const bool running = processor.isSequencerRunning();
    const bool showCursor = hasKeyboardFocus(true) || static_cast<bool>(getProperties()["rd.showCursor"]);
    const float gw = groupWidth();

    const auto labelFont = rd::ui::font(rd::ui::Face::Ui700, 10.5f, 0.16f);
    const auto beatFont = rd::ui::font(rd::ui::Face::Ui700, 14.0f);
    const auto noteFont = rd::ui::font(rd::ui::Face::Vfd, 15.0f);
    const auto numberFont = rd::ui::font(rd::ui::Face::Ui600, 11.0f);
    const auto labelColumn = [](float y, float h) { return juce::Rectangle<float>(0.0f, y, kLabelW, h); };

    for (int r = 0; r < rows; ++r) {
        const float y0 = static_cast<float>(r) * (kRowH + kRowGap);
        const int playing = running && processor.getPlayingPattern() == first + r ? processor.getPlayingStep() : -1;

        g.setColour(t.label);
        g.setFont(labelFont);
        if (rows == 2)
            g.drawText("PAT " + juce::String::charToString(static_cast<juce::juce_wchar>("0123456789ABCDEF"[first + r])),
                       labelColumn(y0, kRulerH), juce::Justification::centredRight, false);
        g.drawText("NOTE", labelColumn(y0 + kNoteY, kNoteH), juce::Justification::centredRight, false);
        g.drawText("ACCENT", labelColumn(y0 + kAccentY, kLedH), juce::Justification::centredRight, false);
        g.drawText("SLIDE", labelColumn(y0 + kSlideY, kLedH), juce::Justification::centredRight, false);
        g.drawText("STEP", labelColumn(y0 + kKeyY, kKeyH), juce::Justification::centredRight, false);

        // Beat ruler and the VFD window of each beat.
        for (int beat = 0; beat < 4; ++beat) {
            const float x = cellX(beat * 4);
            const auto number = juce::String(beat + 1);
            const float nw = rd::ui::textWidth(beatFont, number);
            g.setColour(t.ink);
            g.setFont(beatFont);
            g.drawText(number, juce::Rectangle<float>(x, y0, nw + 1.0f, kRulerH), juce::Justification::centredLeft, false);
            g.setColour(t.ruler);
            g.fillRect(juce::Rectangle<float>(x + nw + 6.0f, y0 + std::floor(kRulerH / 2), gw - nw - 6.0f, 1.0f));
            lf->drawVfdWindow(g, {x, y0 + kNoteY, gw, kNoteH}, 3.0f);
        }

        for (int s = 0; s < kSteps; ++s) {
            const auto& st = stepAt(r, s);
            const auto note = cellBounds(r, s, Row::Note);

            if (s == playing) {
                g.setColour(t.acid);
                g.fillRoundedRectangle(note, 2.0f);
                g.setColour(t.vfd);
                g.setFont(noteFont);
                g.drawText(noteName(st.note), note, juce::Justification::centred, false);
            } else {
                const float alpha = st.note == -2 ? 0.28f : (st.note == -1 ? 0.55f : 1.0f);
                lf->drawGlowText(g, noteName(st.note), noteFont, note, t.acid.withMultipliedAlpha(alpha),
                                 juce::Justification::centred, true);
            }
            if (r == hoverRow && s == hoverStep) {
                g.setColour(t.acid.withAlpha(0.35f));
                g.drawRoundedRectangle(note.reduced(0.5f), 2.0f, 1.0f);
            }
            if (showCursor && r == cursorRow && s == cursorStep) {
                const auto bar = juce::Rectangle<float>(note.getX() + note.getWidth() * 0.22f, note.getBottom() - 6.0f,
                                                        note.getWidth() * 0.56f, 2.0f);
                juce::DropShadow(t.cursor.withAlpha(0.8f), 6, {}).drawForRectangle(g, bar.toNearestInt());
                g.setColour(t.cursor);
                g.fillRect(bar);
            }

            // Accent LED.
            const auto accent = cellBounds(r, s, Row::Accent);
            lf->drawLed(g, accent.getCentre(), 8.0f, st.accent ? t.acid : t.accOff, st.accent != 0, true);

            // Slide: the step glides from the note sounding before it, so a tie
            // comes in from the previous step, under the LEDs (on step 1, from
            // the bar before). Across the pair in 32-step mode it leaves step 16
            // and enters step 17.
            const auto slide = cellBounds(r, s, Row::Slide);
            const float tieY = slide.getY() + 5.0f;
            g.setColour(t.slideTie);
            if (st.slide) {
                const float from = s > 0 ? cellBounds(r, s - 1, Row::Slide).getCentreX() : slide.getX();
                g.fillRect(juce::Rectangle<float>(from, tieY, slide.getCentreX() - from, 2.0f));
            }
            if (s == kSteps - 1 && r == 0 && rows == 2 && stepAt(1, 0).slide)
                g.fillRect(juce::Rectangle<float>(slide.getCentreX(), tieY, 14.0f, 2.0f));
        }
        // LEDs over the ties.
        for (int s = 0; s < kSteps; ++s) {
            const bool on = stepAt(r, s).slide != 0;
            lf->drawLed(g, cellBounds(r, s, Row::Slide).getCentre(), 8.0f, on ? t.slide : t.slideOff, on, true);
        }

        // Step keys: beats 1 and 3 light, 2 and 4 darker, LED bar on top.
        for (int s = 0; s < kSteps; ++s) {
            const auto key = cellBounds(r, s, Row::Step);
            const bool beatA = (s / 4) % 2 == 0;
            lf->drawKey(g, key, 4.0f, beatA ? t.stepA : t.stepB, beatA ? t.stepAHi : t.stepBHi, t.stepLo);
            g.setColour(t.stepNum);
            g.setFont(numberFont);
            g.drawText(juce::String(r * kSteps + s + 1),
                       juce::Rectangle<float>(key.getX(), key.getBottom() - 5.0f - 13.2f, key.getWidth(), 13.2f),
                       juce::Justification::centred, false);
            const auto led = juce::Rectangle<float>(14.0f, 3.0f).withCentre({key.getCentreX(), key.getY() + 6.5f});
            if (s == playing) {
                juce::DropShadow(t.play.withAlpha(0.7f), 8, {}).drawForRectangle(g, led.toNearestInt());
                g.setColour(t.play);
            } else {
                g.setColour(t.playOff);
            }
            g.fillRoundedRectangle(led, 1.5f);
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

std::vector<rd::Pattern> StepEditor::shownPatterns() const
{
    const auto sequence = processor.getSequence();
    const int first = numRows() == 2 ? (patternIndex & ~1) : patternIndex;
    std::vector<rd::Pattern> patterns;
    for (int r = 0; r < numRows(); ++r)
        patterns.push_back(sequence.patterns[static_cast<size_t>(first + r)]);
    return patterns;
}

juce::String StepEditor::shownName() const
{
    static const char* names = "0123456789ABCDEF";
    const int first = numRows() == 2 ? (patternIndex & ~1) : patternIndex;
    juce::String name = juce::String::charToString(static_cast<juce::juce_wchar>(names[first]));
    if (numRows() == 2)
        name << "+" << juce::String::charToString(static_cast<juce::juce_wchar>(names[first + 1]));
    return name;
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

bool StepEditor::canClear() const
{
    for (const auto& pattern : shownPatterns())
        for (const auto& st : pattern.steps)
            if (st.note != -2 || st.accent != 0 || st.slide != 0)
                return true;
    return false;
}

void StepEditor::clear()
{
    if (!canClear())
        return;
    checkpoint();
    const int first = numRows() == 2 ? (patternIndex & ~1) : patternIndex;
    for (int r = 0; r < numRows(); ++r)
        data.patterns[static_cast<size_t>(first + r)] = {};
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
    } else if (kind == Row::Step) {
        cursorRow = row;
        cursorStep = step;
        grabKeyboardFocus();
        repaint();
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

void StepEditor::mouseMove(const juce::MouseEvent& e)
{
    int row = -1, step = -1;
    Row kind = Row::None;
    if (!hit(e.position, row, step, kind) || kind != Row::Note)
        row = step = -1;
    if (row != hoverRow || step != hoverStep) {
        hoverRow = row;
        hoverStep = step;
        repaint();
    }
}

void StepEditor::mouseExit(const juce::MouseEvent&)
{
    if (hoverStep >= 0) {
        hoverRow = hoverStep = -1;
        repaint();
    }
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
