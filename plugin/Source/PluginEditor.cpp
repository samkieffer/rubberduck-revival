#include "PluginEditor.h"

#include <cstring>

#include "MidiExport.h"

namespace {

const juce::Colour kBackground(0xff15171b);
const juce::Colour kPanel(0xff1f2228);
const juce::Colour kText(0xffe6e6e6);
const juce::Colour kDim(0xff8a8f99);
const juce::Colour kAcid(0xffffb000);
const juce::Colour kOff(0xff3c4049);  // controls the selected engine does not have

constexpr int kWidth = 1000;
constexpr int kHeight = 634;  // with a single row of steps

} // namespace

void MidiDragButton::mouseDown(const juce::MouseEvent& e)
{
    dragged = false;
    TextButton::mouseDown(e);
}

void MidiDragButton::mouseDrag(const juce::MouseEvent& e)
{
    if (dragged || e.getDistanceFromDragStart() < 6 || !makeFile)
        return;
    dragged = true;
    const auto file = makeFile();
    if (file.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles({file.getFullPathName()}, false, this);
}

void MidiDragButton::clicked()
{
    if (!dragged && onPlainClick)
        onPlainClick();
}

RubberDuckEditor::RubberDuckEditor(RubberDuckProcessor& p)
    : AudioProcessorEditor(&p), processor(p), stepEditor(p)
{
    lookAndFeel.setColourScheme(juce::LookAndFeel_V4::getMidnightColourScheme());
    lookAndFeel.setColour(juce::Slider::rotarySliderFillColourId, kAcid);
    lookAndFeel.setColour(juce::Slider::thumbColourId, kAcid);
    lookAndFeel.setColour(juce::TextButton::buttonOnColourId, kAcid);
    lookAndFeel.setColour(juce::TextButton::textColourOnId, kBackground);
    lookAndFeel.setColour(juce::ComboBox::backgroundColourId, kPanel);
    setLookAndFeel(&lookAndFeel);

    addKnob("cutoff", "Cut Off");
    addKnob("resonance", "Resonance");
    addKnob("modulation", "Modulation");
    addKnob("squelch", "Squelch");
    addKnob("drive", "Drive");
    addToggle("modification", "m");
    addToggle("tweak", "Tweak");
    addToggle("tweet", "Tweet");
    addKnob("attack", "Attack");
    addKnob("decay", "Decay");
    addKnob("accent", "Accent");
    addChoice("osc1", "Osc 1");
    addChoice("osc2", "Osc 2");
    addToggle("osc2double", "x2");
    addKnob("mix", "Mix");
    addKnob("tune", "Tune");
    addKnob("delaytime", "Delay Time");
    addKnob("delaygain", "Delay Gain");
    addKnob("delayfeedback", "Feedback");
    addKnob("distgain", "Dist Gain");
    addKnob("distdrive", "Dist Drive");
    addKnob("volume", "Volume");
    addChoice("engine", "Engine");
    addChoice("rangestart", "Range from");
    addChoice("rangeend", "Range to");
    addChoice("playmode", "Play mode");
    addChoice("source", "Source");
    addChoice("quantize", "Pattern change");
    addToggle("length32", "32 steps").button.setTooltip("Play each pattern with the next one (0+1, 2+3...) as one "
                                                        "32-step pattern, in Single mode");

    static const char* names[16] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "A", "B", "C", "D", "E", "F"};
    for (int i = 0; i < rd::kNumPatterns; ++i) {
        auto& b = patternButtons[static_cast<size_t>(i)];
        b.setButtonText(names[i]);
        b.setClickingTogglesState(false);
        b.onClick = [this, i] { selectPattern(i); };
        addAndMakeVisible(b);
    }

    auto* patternParam = processor.apvts.getParameter("pattern");
    patternAttachment = std::make_unique<juce::ParameterAttachment>(
        *patternParam, [this](float value) { stepEditor.setPatternIndex(static_cast<int>(value) - 1); });
    patternAttachment->sendInitialUpdate();

    shiftLeft.onClick = [this] { stepEditor.shiftSteps(-1); };
    shiftRight.onClick = [this] { stepEditor.shiftSteps(1); };
    octaveDown.onClick = [this] { stepEditor.transpose(-12); };
    semitoneDown.onClick = [this] { stepEditor.transpose(-1); };
    semitoneUp.onClick = [this] { stepEditor.transpose(1); };
    octaveUp.onClick = [this] { stepEditor.transpose(12); };
    undoButton.onClick = [this] { stepEditor.undo(); };
    redoButton.onClick = [this] { stepEditor.redo(); };
    copyButton.onClick = [this] { stepEditor.copy(); };
    pasteButton.onClick = [this] { stepEditor.paste(); };
    entryOctaveDown.onClick = [this] { stepEditor.setEntryOctave(stepEditor.getEntryOctave() - 1); };
    entryOctaveUp.onClick = [this] { stepEditor.setEntryOctave(stepEditor.getEntryOctave() + 1); };
    shiftLeft.setTooltip("Shift the steps left (Shift+Left)");
    shiftRight.setTooltip("Shift the steps right (Shift+Right)");
    octaveDown.setTooltip("Transpose an octave down (Option+Down)");
    semitoneDown.setTooltip("Transpose a semitone down (Shift+Down)");
    semitoneUp.setTooltip("Transpose a semitone up (Shift+Up)");
    octaveUp.setTooltip("Transpose an octave up (Option+Up)");
    undoButton.setTooltip("Undo (Cmd+Z)");
    redoButton.setTooltip("Redo (Cmd+Shift+Z)");
    copyButton.setTooltip("Copy the shown pattern(s) (Cmd+C)");
    pasteButton.setTooltip("Paste (Cmd+V)");
    midiButton.makeFile = [this] {
        const auto name = "Rubber Duck " + stepEditor.shownName();
        return midiexport::writeTemporary(
            midiexport::fromPatterns(stepEditor.shownPatterns(), processor.getHostBpm(), name), name);
    };
    midiButton.onPlainClick = [this] { saveMidi(); };
    midiButton.setTooltip("Drag onto a Live MIDI track to get the shown pattern(s) as a clip; click to save a .mid file");
    entryOctaveLabel.setJustificationType(juce::Justification::centred);
    entryOctaveLabel.setColour(juce::Label::textColourId, kText);
    entryOctaveLabel.setTooltip("Octave of the lower row of note keys");
    for (auto* b : {&shiftLeft, &shiftRight, &octaveDown, &semitoneDown, &semitoneUp, &octaveUp, &undoButton, &redoButton,
                    &copyButton, &pasteButton, &entryOctaveDown, &entryOctaveUp, static_cast<juce::TextButton*>(&midiButton)}) {
        b->setWantsKeyboardFocus(false);
        addAndMakeVisible(*b);
    }
    addAndMakeVisible(entryOctaveLabel);

    loadButton.onClick = [this] { loadFile(); };
    saveButton.onClick = [this] { saveFile(); };
    addAndMakeVisible(loadButton);
    addAndMakeVisible(saveButton);
    addAndMakeVisible(stepEditor);

    setSize(kWidth, editorHeight());
    startTimerHz(20);
}

RubberDuckEditor::~RubberDuckEditor()
{
    setLookAndFeel(nullptr);
}

RubberDuckEditor::Knob& RubberDuckEditor::addKnob(const char* paramId, const char* text)
{
    auto k = std::make_unique<Knob>();
    k->slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 18);
    k->label.setText(text, juce::dontSendNotification);
    k->label.setJustificationType(juce::Justification::centred);
    k->label.setColour(juce::Label::textColourId, kDim);
    k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, paramId, k->slider);
    addAndMakeVisible(k->slider);
    addAndMakeVisible(k->label);
    k->slider.setComponentID(paramId);
    knobs.push_back(std::move(k));
    return *knobs.back();
}

RubberDuckEditor::Toggle& RubberDuckEditor::addToggle(const char* paramId, const char* text)
{
    auto t = std::make_unique<Toggle>();
    t->button.setButtonText(text);
    t->button.setClickingTogglesState(true);
    t->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.apvts, paramId, t->button);
    t->button.setComponentID(paramId);
    addAndMakeVisible(t->button);
    toggles.push_back(std::move(t));
    return *toggles.back();
}

RubberDuckEditor::Choice& RubberDuckEditor::addChoice(const char* paramId, const char* text)
{
    auto c = std::make_unique<Choice>();
    if (auto* param = dynamic_cast<juce::AudioParameterChoice*>(processor.apvts.getParameter(paramId)))
        c->box.addItemList(param->choices, 1);
    c->label.setText(text, juce::dontSendNotification);
    c->label.setColour(juce::Label::textColourId, kDim);
    c->label.setJustificationType(juce::Justification::centredLeft);
    c->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(processor.apvts, paramId, c->box);
    c->box.setComponentID(paramId);
    addAndMakeVisible(c->box);
    addAndMakeVisible(c->label);
    choices.push_back(std::move(c));
    return *choices.back();
}

int RubberDuckEditor::editorHeight() const
{
    return kHeight + StepEditor::heightFor(stepEditor.numRows()) - StepEditor::heightFor(1);
}

void RubberDuckEditor::selectPattern(int index)
{
    // In 32-step mode a pair is selected by its first pattern.
    if (processor.pairMode())
        index &= ~1;
    patternAttachment->setValueAsCompleteGesture(static_cast<float>(index + 1));
}

void RubberDuckEditor::loadFile()
{
    chooser = std::make_unique<juce::FileChooser>("Load a Rubber Duck machine state", juce::File(), "*.h30;*.H30");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this](const juce::FileChooser& fc) {
                             const auto file = fc.getResult();
                             if (file == juce::File())
                                 return;
                             juce::String error;
                             if (!processor.importH30(file, error))
                                 juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                                        "Rubber Duck Revival", file.getFileName() + ": " + error);
                             stepEditor.reload();
                         });
}

void RubberDuckEditor::saveFile()
{
    chooser = std::make_unique<juce::FileChooser>("Save machine state", juce::File(), "*.h30");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                         [this](const juce::FileChooser& fc) {
                             auto file = fc.getResult();
                             if (file == juce::File())
                                 return;
                             if (!file.hasFileExtension("h30"))
                                 file = file.withFileExtension("h30");
                             processor.exportH30(file);
                         });
}

void RubberDuckEditor::saveMidi()
{
    const auto name = "Rubber Duck " + stepEditor.shownName();
    chooser = std::make_unique<juce::FileChooser>("Save the pattern as MIDI",
                                                  juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                                                      .getChildFile(name + ".mid"),
                                                  "*.mid");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                         [this, name](const juce::FileChooser& fc) {
                             auto file = fc.getResult();
                             if (file == juce::File())
                                 return;
                             if (!file.hasFileExtension("mid"))
                                 file = file.withFileExtension("mid");
                             file.deleteFile();
                             juce::FileOutputStream out(file);
                             midiexport::fromPatterns(stepEditor.shownPatterns(), processor.getHostBpm(), name).writeTo(out);
                         });
}

void RubberDuckEditor::updateEngineControls()
{
    // v1.02 has no Squelch/Drive filter branch, no triangle and 14 patterns;
    // its sequence modes play a pattern range instead of the song.
    const bool v1 = processor.getEngineKind() == RubberDuckProcessor::EngineKind::V102;
    // Disabled controls lose their acid colour so they read as absent, not just dimmed.
    auto enable = [this](const char* id, bool on) {
        for (auto& k : knobs)
            if (k->slider.getComponentID() == id) {
                k->slider.setEnabled(on);
                k->label.setEnabled(on);
                for (auto colour : {juce::Slider::rotarySliderFillColourId, juce::Slider::thumbColourId})
                    if (on)
                        k->slider.removeColour(colour);
                    else
                        k->slider.setColour(colour, kOff);
            }
        for (auto& t : toggles)
            if (t->button.getComponentID() == id) {
                t->button.setEnabled(on);
                if (on)
                    t->button.removeColour(juce::TextButton::buttonOnColourId);
                else
                    t->button.setColour(juce::TextButton::buttonOnColourId, kOff);
            }
        for (auto& c : choices)
            if (c->box.getComponentID() == id) {
                c->box.setEnabled(on);
                c->label.setEnabled(on);
            }
    };
    for (const char* id : {"squelch", "drive", "modification", "tweak", "tweet"})
        enable(id, !v1);
    // The range is only played by the v1.02 Sequence and Random modes.
    const bool single = processor.apvts.getRawParameterValue("playmode")->load() < 0.5f;
    for (const char* id : {"rangestart", "rangeend"})
        enable(id, v1 && !single);
    for (auto& c : choices)
        if (c->box.getComponentID() == "osc1")
            c->box.setItemEnabled(1, !v1);
    for (int i = 0; i < rd::kNumPatterns; ++i)
        patternButtons[static_cast<size_t>(i)].setEnabled(i < processor.numPatterns());
    repaint();
}

void RubberDuckEditor::timerCallback()
{
    const int engineKind = static_cast<int>(processor.getEngineKind());
    const int playMode = static_cast<int>(std::lround(processor.apvts.getRawParameterValue("playmode")->load()));
    if (engineKind != shownEngine || playMode != shownPlayMode) {
        shownEngine = engineKind;
        shownPlayMode = playMode;
        updateEngineControls();
    }
    const int rows = stepEditor.numRows();
    const bool rowsChanged = rows != shownRows;
    if (rowsChanged) {
        shownRows = rows;
        setSize(kWidth, editorHeight());
    }
    undoButton.setEnabled(stepEditor.canUndo());
    redoButton.setEnabled(stepEditor.canRedo());
    pasteButton.setEnabled(stepEditor.canPaste());
    entryOctaveLabel.setText("Oct " + juce::String(stepEditor.getEntryOctave()), juce::dontSendNotification);

    const int selected = stepEditor.getPatternIndex();
    const int playing = processor.isSequencerRunning() ? processor.getPlayingPattern() : -1;
    if (selected != shownPattern || playing != shownPlaying || rowsChanged) {
        shownPattern = selected;
        shownPlaying = playing;
        for (int i = 0; i < rd::kNumPatterns; ++i) {
            auto& b = patternButtons[static_cast<size_t>(i)];
            b.setToggleState(rows == 2 ? (i & ~1) == (selected & ~1) : i == selected, juce::dontSendNotification);
            b.setColour(juce::TextButton::buttonColourId, i == playing && i != selected ? kAcid.withAlpha(0.35f) : kPanel);
        }
    }
}

void RubberDuckEditor::paint(juce::Graphics& g)
{
    g.fillAll(kBackground);
    g.setColour(kAcid);
    g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    g.drawText("RUBBER DUCK", 16, 8, 200, 30, juce::Justification::centredLeft);
    g.setColour(kText);
    g.setFont(juce::FontOptions(16.0f));
    g.drawText("revival", 168, 12, 100, 26, juce::Justification::centredLeft);
    g.setColour(kDim);
    g.setFont(juce::FontOptions(12.0f));
    g.drawText(juce::String(processor.getEngineKind() == RubberDuckProcessor::EngineKind::V102 ? "1997 engine" : "2000 engine")
                   + "  v" JucePlugin_VersionString,
               168, 30, 120, 14, juce::Justification::centredLeft);

    for (const auto& s : sections) {
        g.setColour(kPanel);
        g.fillRoundedRectangle(s.bounds.toFloat(), 6.0f);
        g.setColour(kDim);
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(s.title, s.bounds.reduced(10, 4).removeFromTop(14), juce::Justification::topLeft);
    }
    g.setColour(kDim);
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    for (const auto& c : captions)
        g.drawText(c.title, c.bounds, juce::Justification::centredRight);
}

void RubberDuckEditor::layoutKnobs(juce::Rectangle<int> area, std::initializer_list<juce::Component*> items)
{
    const int w = area.getWidth() / static_cast<int>(items.size());
    for (auto* c : items) {
        auto cell = area.removeFromLeft(w).reduced(4, 0);
        if (auto* slider = dynamic_cast<juce::Slider*>(c)) {
            for (auto& k : knobs)
                if (&k->slider == slider) {
                    k->label.setBounds(cell.removeFromTop(16));
                    break;
                }
            slider->setBounds(cell);
        } else if (dynamic_cast<juce::ComboBox*>(c) != nullptr) {
            for (auto& ch : choices)
                if (&ch->box == c) {
                    ch->label.setBounds(cell.removeFromTop(16));
                    break;
                }
            c->setBounds(cell.removeFromTop(26).withY(cell.getY() + 20));
        } else {
            c->setBounds(cell.withSizeKeepingCentre(cell.getWidth(), 28));
        }
    }
}

void RubberDuckEditor::resized()
{
    auto find = [this](const char* id) -> juce::Component* { return findChildWithID(id); };
    auto area = getLocalBounds().reduced(12);

    auto header = area.removeFromTop(40);
    header.removeFromLeft(240);
    saveButton.setBounds(header.removeFromRight(90).reduced(2, 6));
    loadButton.setBounds(header.removeFromRight(90).reduced(2, 6));
    for (const char* id : {"quantize", "source", "playmode", "engine"}) {
        for (auto& c : choices)
            if (c->box.getComponentID() == id) {
                auto cell = header.removeFromRight(std::strcmp(id, "engine") == 0 ? 150 : 130);
                c->label.setBounds(cell.removeFromTop(14).translated(0, -2));
                c->box.setBounds(cell.reduced(2, 2));
            }
    }

    area.removeFromTop(6);
    auto patternRow = area.removeFromTop(30);
    const int bw = patternRow.getWidth() / rd::kNumPatterns;
    for (auto& b : patternButtons)
        b.setBounds(patternRow.removeFromLeft(bw).reduced(2, 0));

    area.removeFromTop(6);
    {
        captions.clear();
        auto bar = area.removeFromTop(26);
        auto group = [&](const char* title, int titleWidth, std::initializer_list<juce::Component*> items, int itemWidth) {
            captions.push_back({title, bar.removeFromLeft(titleWidth).withTrimmedRight(6)});
            for (auto* c : items)
                c->setBounds(bar.removeFromLeft(itemWidth).reduced(2, 0));
            bar.removeFromLeft(14);
        };
        group("SHIFT", 44, {&shiftLeft, &shiftRight}, 34);
        group("TRANSPOSE", 74, {&octaveDown, &semitoneDown, &semitoneUp, &octaveUp}, 40);
        group("EDIT", 40, {&undoButton, &redoButton, &copyButton, &pasteButton}, 52);
        midiButton.setBounds(bar.removeFromLeft(88).reduced(2, 0));
        if (auto* length = find("length32"))
            length->setBounds(bar.removeFromRight(86).reduced(2, 0));
        bar.removeFromRight(14);
        entryOctaveUp.setBounds(bar.removeFromRight(28).reduced(2, 0));
        entryOctaveLabel.setBounds(bar.removeFromRight(52));
        entryOctaveDown.setBounds(bar.removeFromRight(28).reduced(2, 0));
        captions.push_back({"KEYS", bar.removeFromRight(46).withTrimmedRight(6)});
    }

    area.removeFromTop(8);
    stepEditor.setBounds(area.removeFromTop(StepEditor::heightFor(stepEditor.numRows()) + 8));
    area.removeFromTop(10);

    sections.clear();
    auto top = area.removeFromTop(area.getHeight() / 2).reduced(0, 4);
    auto bottom = area.reduced(0, 4);

    auto filter = top.removeFromLeft(top.getWidth() * 62 / 100).reduced(4, 0);
    sections.push_back({"FILTER", filter});
    auto env = top.reduced(4, 0);
    sections.push_back({"ENVELOPE", env});

    auto osc = bottom.removeFromLeft(bottom.getWidth() * 34 / 100).reduced(4, 0);
    auto fx = bottom.removeFromLeft(bottom.getWidth() * 61 / 100).reduced(4, 0);
    auto master = bottom.reduced(4, 0);
    sections.push_back({"OSCILLATORS", osc});
    sections.push_back({"EFFECTS", fx});
    sections.push_back({"MASTER", master});

    auto inner = [](juce::Rectangle<int> r) { return r.reduced(8, 6).withTrimmedTop(14); };

    {
        auto r = inner(filter);
        auto toggleColumn = r.removeFromRight(70);
        layoutKnobs(r, {find("cutoff"), find("resonance"), find("modulation"), find("squelch"), find("drive")});
        const int th = toggleColumn.getHeight() / 3;
        for (const char* id : {"modification", "tweak", "tweet"})
            if (auto* c = find(id))
                c->setBounds(toggleColumn.removeFromTop(th).reduced(4, 6));
    }
    layoutKnobs(inner(env), {find("attack"), find("decay"), find("accent")});
    {
        auto r = inner(osc);
        auto left = r.removeFromLeft(r.getWidth() / 2);
        auto c1 = left.removeFromTop(left.getHeight() / 2);
        for (auto& c : choices) {
            if (c->box.getComponentID() == "osc1") {
                c->label.setBounds(c1.removeFromTop(16));
                c->box.setBounds(c1.removeFromTop(26));
            } else if (c->box.getComponentID() == "osc2") {
                c->label.setBounds(left.removeFromTop(16));
                auto row = left.removeFromTop(26);
                if (auto* x2 = find("osc2double"))
                    x2->setBounds(row.removeFromRight(40).withTrimmedLeft(4));
                c->box.setBounds(row);
            }
        }
        layoutKnobs(r, {find("mix"), find("tune")});
    }
    layoutKnobs(inner(fx), {find("delaytime"), find("delaygain"), find("delayfeedback"), find("distgain"), find("distdrive")});
    {
        auto r = inner(master);
        auto ranges = r.removeFromRight(r.getWidth() * 55 / 100).reduced(4, 0);
        layoutKnobs(r, {find("volume")});
        const int h = ranges.getHeight() / 2;
        for (const char* id : {"rangestart", "rangeend"})
            for (auto& c : choices)
                if (c->box.getComponentID() == id) {
                    auto cell = ranges.removeFromTop(h);
                    c->label.setBounds(cell.removeFromTop(16));
                    c->box.setBounds(cell.removeFromTop(26));
                }
    }
}
