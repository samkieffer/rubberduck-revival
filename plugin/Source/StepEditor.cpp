#include "StepEditor.h"

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

} // namespace

StepEditor::StepEditor(RubberDuckProcessor& p) : processor(p)
{
    reload();
    startTimerHz(30);
}

void StepEditor::setPatternIndex(int index)
{
    patternIndex = juce::jlimit(0, rd::kNumPatterns - 1, index);
    repaint();
}

void StepEditor::reload()
{
    data = processor.getSequence();
    repaint();
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

juce::Rectangle<float> StepEditor::cellBounds(int step, Row row) const
{
    const float w = static_cast<float>(getWidth()) / rd::kStepsPerPattern;
    const float x = w * static_cast<float>(step) + 2.0f;
    const float cw = w - 4.0f;
    switch (row) {
    case Row::Note: return {x, kHeaderH, cw, kNoteH};
    case Row::Accent: return {x, kHeaderH + kNoteH + kGap, cw, kToggleH};
    case Row::Slide: return {x, kHeaderH + kNoteH + kToggleH + 2 * kGap, cw, kToggleH};
    case Row::None: break;
    }
    return {};
}

bool StepEditor::hit(juce::Point<float> p, int& step, Row& row) const
{
    for (int s = 0; s < rd::kStepsPerPattern; ++s)
        for (Row r : {Row::Note, Row::Accent, Row::Slide})
            if (cellBounds(s, r).contains(p)) {
                step = s;
                row = r;
                return true;
            }
    return false;
}

void StepEditor::paint(juce::Graphics& g)
{
    g.fillAll(kBackground);
    const auto& steps = data.patterns[static_cast<size_t>(patternIndex)].steps;
    const bool showPlaying = processor.isSequencerRunning() && processor.getPlayingPattern() == patternIndex;
    const int playing = showPlaying ? processor.getPlayingStep() : -1;

    for (int s = 0; s < rd::kStepsPerPattern; ++s) {
        const auto& st = steps[static_cast<size_t>(s)];
        auto note = cellBounds(s, Row::Note);

        g.setColour(s == playing ? kPlay : (s % 4 == 0 ? kDim : kCell));
        g.fillRoundedRectangle(note.getX() + 4, 4, note.getWidth() - 8, 6, 2);

        g.setColour(kCell);
        g.fillRoundedRectangle(note, 4);
        g.setColour(st.note >= 0 ? kText : kDim);
        g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        g.drawText(noteName(st.note), note, juce::Justification::centred);

        for (Row r : {Row::Accent, Row::Slide}) {
            auto b = cellBounds(s, r);
            const bool on = r == Row::Accent ? st.accent != 0 : st.slide != 0;
            g.setColour(on ? (r == Row::Accent ? kAcid : kSlide) : kCell);
            g.fillRoundedRectangle(b, 3);
            g.setColour(on ? kBackground : kDim);
            g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
            g.drawText(r == Row::Accent ? "AC" : "SL", b, juce::Justification::centred);
        }
    }
}

void StepEditor::commit()
{
    processor.setSequence(data);
    repaint();
}

void StepEditor::setNote(int step, int note)
{
    auto& st = data.patterns[static_cast<size_t>(patternIndex)].steps[static_cast<size_t>(step)];
    st.note = static_cast<int8_t>(juce::jlimit(-2, 120, note));
    if (note >= 0)
        lastNote = note;
    commit();
}

void StepEditor::mouseDown(const juce::MouseEvent& e)
{
    data = processor.getSequence();
    int step;
    Row row;
    if (!hit(e.position, step, row))
        return;
    auto& st = data.patterns[static_cast<size_t>(patternIndex)].steps[static_cast<size_t>(step)];
    if (e.mods.isPopupMenu()) {
        showStepMenu(step);
        return;
    }
    if (row == Row::Accent) {
        st.accent = st.accent ? 0 : 1;
        commit();
    } else if (row == Row::Slide) {
        st.slide = st.slide ? 0 : 1;
        commit();
    } else if (row == Row::Note) {
        dragStep = step;
        dragStartNote = st.note >= 0 ? st.note : lastNote;
        dragStartY = e.position.y;
    }
}

void StepEditor::mouseDrag(const juce::MouseEvent& e)
{
    if (dragStep < 0)
        return;
    const int delta = static_cast<int>((dragStartY - e.position.y) / 6.0f);
    if (delta != 0 || data.patterns[static_cast<size_t>(patternIndex)].steps[static_cast<size_t>(dragStep)].note >= 0)
        setNote(dragStep, juce::jlimit(0, 120, dragStartNote + delta));
}

void StepEditor::mouseUp(const juce::MouseEvent&)
{
    dragStep = -1;
}

void StepEditor::mouseDoubleClick(const juce::MouseEvent& e)
{
    int step;
    Row row;
    if (!hit(e.position, step, row) || row != Row::Note)
        return;
    const auto& st = data.patterns[static_cast<size_t>(patternIndex)].steps[static_cast<size_t>(step)];
    setNote(step, st.note >= 0 ? -2 : lastNote);
}

void StepEditor::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    int step;
    Row row;
    if (!hit(e.position, step, row) || row != Row::Note)
        return;
    data = processor.getSequence();
    const auto& st = data.patterns[static_cast<size_t>(patternIndex)].steps[static_cast<size_t>(step)];
    const int base = st.note >= 0 ? st.note : lastNote;
    setNote(step, juce::jlimit(0, 120, base + (wheel.deltaY > 0 ? 1 : -1)));
}

void StepEditor::showStepMenu(int step)
{
    juce::PopupMenu m;
    m.addItem(1, "Rest");
    m.addItem(2, "Note off");
    m.addSeparator();
    m.addItem(3, "Octave up");
    m.addItem(4, "Octave down");
    m.addSeparator();
    m.addItem(5, "Clear pattern");
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [this, step](int r) {
        data = processor.getSequence();
        auto& pattern = data.patterns[static_cast<size_t>(patternIndex)];
        const int note = pattern.steps[static_cast<size_t>(step)].note;
        switch (r) {
        case 1: setNote(step, -2); break;
        case 2: setNote(step, -1); break;
        case 3: if (note >= 0) setNote(step, note + 12); break;
        case 4: if (note >= 0) setNote(step, note - 12); break;
        case 5:
            pattern = rd::Pattern{};
            commit();
            break;
        default: break;
        }
    });
}

void StepEditor::timerCallback()
{
    const int playing = processor.isSequencerRunning() ? processor.getPlayingStep() : -1;
    if (playing != shownStep) {
        shownStep = playing;
        repaint();
    }
}
