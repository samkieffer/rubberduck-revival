#include "PluginEditor.h"

#include <cmath>

#include "MidiExport.h"

using rd::ui::Face;
using rd::ui::ThemeKind;

namespace {

constexpr int kWidth = 1000;
constexpr int kHeight = 634;  // with a single row of steps
// Chassis margins and the vertical bands of the 16-step layout.
constexpr int kLeft = 18, kRight = 982;
constexpr int kHeaderY = 14, kHeaderH = 40;
constexpr int kPatternY = 62, kPatternH = 44;
constexpr int kSequencerY = 120, kSequencerH = 182;
constexpr int kRow1Y = 318, kRow1H = 148;
constexpr int kRow2Y = 482, kRow2H = 140;
constexpr int kPatternX = kLeft + 66 + 6;  // first pattern column
constexpr int kPatternPitch = 34;          // 28 px columns, 6 px apart

const juce::String kRangeTooltip("Engine v1.02, Sequence or Shuffle only");

juce::Font sectionTitleFont()
{
    return rd::ui::font(Face::Ui700, 11.0f, 0.22f);
}

// "RUBDUCK" in the header.
juce::Font nameFont()
{
    return rd::ui::font(Face::Name700, 13.0f, 0.08f);
}

// Text width as CSS lays it out: like JUCE, letter spacing follows every letter, the last one too.
int cssWidth(const juce::Font& font, const juce::String& text)
{
    return static_cast<int>(std::ceil(rd::ui::textWidth(font, text)));
}

void styleKey(juce::TextButton& b, float fontSize, float tracking = 0.0f, float radius = 3.0f)
{
    b.getProperties().set("rd.fontSize", fontSize);
    b.getProperties().set("rd.tracking", tracking);
    b.getProperties().set("rd.radius", radius);
}

// Width of a toolbar key: its text plus padding, at least `minWidth`.
int keyWidth(RdLookAndFeel& lf, juce::TextButton& b, int minWidth, int padding)
{
    if (b.getProperties()["rd.icon"].toString().isNotEmpty())
        return minWidth;
    return std::max(minWidth, cssWidth(lf.getTextButtonFont(b, 22), b.getButtonText()) + 2 * padding);
}

void drawScrew(juce::Graphics& g, const rd::ui::Theme& t, juce::Point<float> topLeft, juce::Line<float> slot)
{
    const auto circle = juce::Rectangle<float>(10.0f, 10.0f).withCentre(topLeft.translated(6.0f, 6.0f));
    g.setColour(t.screw);
    g.fillEllipse(circle);
    g.setColour(t.screwRim);
    g.drawEllipse(circle, 1.0f);
    g.setColour(t.screwSlot);
    g.drawLine(slot.getStartX() + topLeft.x, slot.getStartY() + topLeft.y, slot.getEndX() + topLeft.x,
               slot.getEndY() + topLeft.y, 1.4f);
}

} // namespace

// ---------------------------------------------------------------------------

int MidiDragButton::getIdealWidth() const
{
    return 12 + 10 + 8 + cssWidth(rd::ui::font(Face::Ui600, 13.0f, 0.14f), getButtonText()) + 12;
}

void MidiDragButton::paintButton(juce::Graphics& g, bool, bool)
{
    auto* lf = RdLookAndFeel::of(*this);
    if (lf == nullptr)
        return;
    const auto& t = lf->theme();
    const auto r = getLocalBounds().toFloat().reduced(0.5f);
    juce::Path outline, dashes;
    outline.addRoundedRectangle(r, 4.0f);
    const float pattern[] = {3.0f, 3.0f};
    juce::PathStrokeType(1.0f).createDashedStroke(dashes, outline, pattern, 2);
    g.setColour(t.dashed);
    g.fillPath(dashes);

    // Three lines, then the name.
    const float cy = r.getCentreY();
    juce::Path icon;
    for (float dy : {-4.0f, 0.0f, 4.0f}) {
        icon.startNewSubPath(13.0f, cy + dy);
        icon.lineTo(21.0f, cy + dy);
    }
    g.setColour(t.label);
    g.strokePath(icon, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(t.ink);
    g.setFont(rd::ui::font(Face::Ui600, 13.0f, 0.14f));
    g.drawText(getButtonText(), getLocalBounds().withTrimmedLeft(12 + 10 + 8), juce::Justification::centredLeft, false);
}

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

// ---------------------------------------------------------------------------

RubberDuckEditor::RubberDuckEditor(RubberDuckProcessor& p)
    : AudioProcessorEditor(&p),
      processor(p),
      range(p.apvts),
      osc1(p.apvts, "osc1", {"TRIANGLE", "SAW/PULSE", "SAW", "SQUARE"}),
      osc2(p.apvts, "osc2", {"SINE", "SAW", "NOISE"}),
      stepEditor(p)
{
    themeKind = seenPreference = rd::ui::preferredTheme();
    lookAndFeel.setTheme(rd::ui::Theme::get(themeKind));
    setLookAndFeel(&lookAndFeel);
    // The tip has rounded corners: let the editor show through them.
    tooltipWindow.setOpaque(false);

    // Big knobs: the ones played live.
    addKnob("cutoff", "Cut Off", true);
    addKnob("resonance", "Resonance", true);
    addKnob("modulation", "Modulation", true);
    addKnob("squelch", "Squelch", true);
    addKnob("drive", "Drive", true);
    addKnob("attack", "Attack", true);
    addKnob("decay", "Decay", true);
    addKnob("accent", "Accent", true);
    addKnob("mix", "Mix", true);
    addKnob("distgain", "Dist Gain", true);
    addKnob("distdrive", "Dist Drive", true);
    auto& tune = addKnob("tune", "Tune", false, true);
    addKnob("delaytime", "Dly Time", false);
    addKnob("delaygain", "Dly Gain", false);
    addKnob("delayfeedback", "Feedback", false);
    addKnob("volume", "Volume", false);
    tune.textFromValueFunction = [](double value) {
        const int v = juce::roundToInt(value);
        return v == 0 ? juce::String(juce::CharPointer_UTF8("\xc2\xb1" "0")) : (v > 0 ? "+" : "") + juce::String(v);
    };
    tune.valueFromTextFunction = [](const juce::String& text) {
        return text.retainCharacters("-0123456789").getDoubleValue();
    };
    tune.updateText();

    addToggle("modification", "M");
    addToggle("tweak", "TWEAK");
    addToggle("tweet", "TWEET");
    addToggle("osc2double", juce::CharPointer_UTF8("\xc3\x97" "2"), true);
    toggle("modification").setTooltip("Filter modification");
    toggle("osc2double").setTooltip("Osc 2 an octave up");

    config.addSegment(p.apvts, "engine", "ENG ", {"1.02", "2.12"}, "Engine: v1.02 (1997) or v2.12 (2000)");
    config.addSegment(p.apvts, "playmode", {}, {"SINGLE", "SEQUENCE", "SHUFFLE"}, "Play mode");
    config.addSegment(p.apvts, "source", {}, {"PATTERNS", "MIDI NOTES"},
                      "Sequencer source: the patterns, or MIDI notes played live");
    config.addSegment(p.apvts, "quantize", {}, {"NEXT BAR", "NEXT STEP", "IMMEDIATE"},
                      "When a new pattern takes over");
    addAndMakeVisible(config);

    duck.onClick = [this] { showThemeMenu(); };
    addAndMakeVisible(duck);
    preset.onClick = [this] { loadFile(); };
    addAndMakeVisible(preset);

    osc1.isChoiceEnabled = [this](int i) { return i != 0 || !isV1(); };  // no triangle in v1.02
    osc1.setTooltip("Oscillator 1 waveform");
    osc2.setTooltip("Oscillator 2 waveform");
    addAndMakeVisible(osc1);
    addAndMakeVisible(osc2);
    range.setTooltip(kRangeTooltip);
    addAndMakeVisible(range);

    stepsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, "length32", stepsSwitch);
    stepsSwitch.setTooltip("Play each pattern with the next one (0+1, 2+3...) as one 32-step pattern, in Single mode");
    addAndMakeVisible(stepsSwitch);

    static const char* names = "0123456789ABCDEF";
    for (int i = 0; i < rd::kNumPatterns; ++i) {
        auto key = std::make_unique<PatternKey>(juce::String::charToString(static_cast<juce::juce_wchar>(names[i])));
        key->onClick = [this, i] { selectPattern(i); };
        addAndMakeVisible(*key);
        patternKeys[static_cast<size_t>(i)] = std::move(key);
    }

    auto* patternParam = processor.apvts.getParameter("pattern");
    patternAttachment = std::make_unique<juce::ParameterAttachment>(
        *patternParam, [this](float value) { stepEditor.setPatternIndex(static_cast<int>(value) - 1); });
    patternAttachment->sendInitialUpdate();

    shiftLeft.getProperties().set("rd.icon", "left");
    shiftRight.getProperties().set("rd.icon", "right");
    for (auto* b : {&shiftLeft, &shiftRight})
        styleKey(*b, 11.0f);
    for (auto* b : {&octaveDown, &semitoneDown, &semitoneUp, &octaveUp})
        styleKey(*b, 12.0f);
    for (auto* b : {&undoButton, &redoButton, &copyButton, &pasteButton, &clearButton})
        styleKey(*b, 12.0f, 0.1f);
    for (auto* b : {&entryOctaveDown, &entryOctaveUp})
        styleKey(*b, 13.0f);
    for (auto* b : {&loadButton, &saveButton})
        styleKey(*b, 13.0f, 0.14f, 4.0f);

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
    clearButton.onClick = [this] {
        if (clearArmedFor.isEmpty()) {
            clearArmedFor = stepEditor.shownName();
            clearArmedAt = juce::Time::getMillisecondCounter();
            clearButton.setButtonText("SURE?");
        } else {
            stepEditor.clear();
            disarmClear();
        }
    };
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
    clearButton.setTooltip("Clear the shown pattern(s): click, then click again to confirm");
    entryOctaveDown.setTooltip("Lower the octave of the note keys");
    entryOctaveUp.setTooltip("Raise the octave of the note keys");
    entryOctave.setTooltip("Octave of the lower row of note keys");
    midiButton.makeFile = [this] {
        const auto name = "RubDuck " + stepEditor.shownName();
        return midiexport::writeTemporary(
            midiexport::fromPatterns(stepEditor.shownPatterns(), processor.getHostBpm(), name), name);
    };
    midiButton.onPlainClick = [this] { saveMidi(); };
    midiButton.setTooltip("Drag onto a Live MIDI track to get the shown pattern(s) as a clip; click to save a .mid file");
    midiButton.setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    for (auto* b : {&shiftLeft, &shiftRight, &octaveDown, &semitoneDown, &semitoneUp, &octaveUp, &undoButton, &redoButton,
                    &copyButton, &pasteButton, &clearButton, &entryOctaveDown, &entryOctaveUp, static_cast<juce::TextButton*>(&midiButton),
                    &loadButton, &saveButton}) {
        b->setWantsKeyboardFocus(false);
        addAndMakeVisible(*b);
    }
    addAndMakeVisible(entryOctave);

    loadButton.onClick = [this] { loadFile(); };
    saveButton.onClick = [this] { saveFile(); };
    loadButton.setTooltip("Load a machine state (.h30) of the original or of RubDuck");
    saveButton.setTooltip("Save the machine state as a .h30 file");
    if (p.hasOwnTransport()) {
        playButton.setWantsKeyboardFocus(false);
        playButton.onClick = [this] { togglePlay(); };
        playButton.setTooltip("Play or stop the sequence (Space)");
        addAndMakeVisible(playButton);
        tempo.onChange = [this](int knob) { processor.setTempoKnob(knob); };
        tempo.setTooltip("Tempo: drag up or down, or scroll. Loading a .h30 sets its tempo");
        tempo.setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        addAndMakeVisible(tempo);
        // Space plays and stops when no control takes it, too.
        setWantsKeyboardFocus(true);
    }
    addAndMakeVisible(stepEditor);
    // The knobs made their value boxes before joining the editor, with JUCE's
    // default look and feel (outline, system font): make them again with ours.
    sendLookAndFeelChange();

    setSize(kWidth, editorHeight());
    timerCallback();
    startTimerHz(20);
}

RubberDuckEditor::~RubberDuckEditor()
{
    setLookAndFeel(nullptr);
}

RdKnob& RubberDuckEditor::addKnob(const char* paramId, const char* text, bool big, bool bipolar)
{
    auto k = std::make_unique<Knob>(text, big, bipolar);
    k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, paramId, k->slider);
    k->slider.setComponentID(paramId);
    addAndMakeVisible(k->slider);
    knobs.emplace_back(paramId, std::move(k));
    return knobs.back().second->slider;
}

LedKey& RubberDuckEditor::addToggle(const char* paramId, const juce::String& text, bool compact)
{
    auto t = std::make_unique<Toggle>(text, compact);
    t->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.apvts, paramId, t->button);
    t->button.setComponentID(paramId);
    addAndMakeVisible(t->button);
    toggles.emplace_back(paramId, std::move(t));
    return toggles.back().second->button;
}

RdKnob& RubberDuckEditor::knob(const char* paramId)
{
    for (auto& [id, k] : knobs)
        if (id == paramId)
            return k->slider;
    jassertfalse;
    return knobs.front().second->slider;
}

LedKey& RubberDuckEditor::toggle(const char* paramId)
{
    for (auto& [id, t] : toggles)
        if (id == paramId)
            return t->button;
    jassertfalse;
    return toggles.front().second->button;
}

int RubberDuckEditor::playMode() const
{
    return static_cast<int>(std::lround(processor.apvts.getRawParameterValue("playmode")->load()));
}

int RubberDuckEditor::editorHeight() const
{
    return kHeight + StepEditor::heightFor(stepEditor.numRows()) - StepEditor::heightFor(1);
}

void RubberDuckEditor::setTheme(ThemeKind kind, bool remember)
{
    if (remember) {
        rd::ui::setPreferredTheme(kind);
        seenPreference = kind;
    }
    themeKind = kind;
    lookAndFeel.setTheme(rd::ui::Theme::get(kind));
    sendLookAndFeelChange();
    resized();
    repaint();
}

void RubberDuckEditor::showThemeMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "Dark theme", true, themeKind == ThemeKind::Dark);
    menu.addItem(2, "Light theme", true, themeKind == ThemeKind::Light);
    menu.addSeparator();
    menu.addItem(3, JucePlugin_Name " " JucePlugin_VersionString, false);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&duck),
                       [safe = juce::Component::SafePointer<RubberDuckEditor>(this)](int result) {
                           if (safe != nullptr && (result == 1 || result == 2))
                               safe->setTheme(result == 2 ? ThemeKind::Light : ThemeKind::Dark, true);
                       });
}

void RubberDuckEditor::disarmClear()
{
    clearArmedFor.clear();
    clearButton.setButtonText("CLEAR");
}

void RubberDuckEditor::showMachineFile()
{
    const auto file = processor.getMachineFile();
    if (file != shownFile) {
        shownFile = file;
        preset.setFile(file);
    }
    // In a host the title bar is the host's; the standalone app's is ours.
    if (processor.wrapperType != juce::AudioProcessor::wrapperType_Standalone)
        return;
    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>()) {
        const juce::String app = JucePlugin_Name;
        const juce::String dash = juce::CharPointer_UTF8(" \xe2\x80\x94 ");
        const auto title = file == juce::File() ? app : file.getFileNameWithoutExtension() + dash + app;
        if (window->getName() != title) {
            window->setName(title);
            if (auto* peer = window->getPeer())
                peer->setRepresentedFile(file);  // the file's icon in the title bar
        }
    }
}

void RubberDuckEditor::togglePlay()
{
    processor.setTransportPlaying(!processor.isTransportPlaying());
    showPlaying();
}

void RubberDuckEditor::showPlaying()
{
    const bool playing = processor.isTransportPlaying();
    playButton.setButtonText(playing ? "STOP" : "PLAY");
    playButton.setToggleState(playing, juce::dontSendNotification);
}

bool RubberDuckEditor::keyPressed(const juce::KeyPress& key)
{
    if (processor.hasOwnTransport() && key == juce::KeyPress(juce::KeyPress::spaceKey)) {
        togglePlay();
        return true;
    }
    return false;
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
    // Opens in the folder of the current file, if any.
    const auto current = processor.getMachineFile();
    chooser = std::make_unique<juce::FileChooser>("Load a machine state",
                                                  current == juce::File() ? juce::File() : current.getParentDirectory(),
                                                  "*.h30;*.H30");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this](const juce::FileChooser& fc) {
                             const auto file = fc.getResult();
                             if (file == juce::File())
                                 return;
                             juce::String error;
                             if (processor.importH30(file, error))
                                 processor.setMachineFile(file);
                             else
                                 juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                                        JucePlugin_Name, file.getFileName() + ": " + error);
                             stepEditor.reload();
                         });
}

void RubberDuckEditor::saveFile()
{
    // Proposes the current file (macOS asks before replacing it).
    chooser = std::make_unique<juce::FileChooser>("Save machine state", processor.getMachineFile(), "*.h30");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                         [this](const juce::FileChooser& fc) {
                             auto file = fc.getResult();
                             if (file == juce::File())
                                 return;
                             if (!file.hasFileExtension("h30"))
                                 file = file.withFileExtension("h30");
                             if (processor.exportH30(file))
                                 processor.setMachineFile(file);
                             else
                                 juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                                        JucePlugin_Name,
                                                                        "Could not save " + file.getFileName());
                         });
}

void RubberDuckEditor::saveMidi()
{
    const auto name = "RubDuck " + stepEditor.shownName();
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
    const bool v1 = isV1();
    for (const char* id : {"squelch", "drive"})
        knob(id).setEnabled(!v1);
    for (const char* id : {"modification", "tweak", "tweet"})
        toggle(id).setEnabled(!v1);
    // The range is only played by the v1.02 Sequence and Random modes.
    range.setActive(v1 && playMode() != 0);
    for (int i = 0; i < rd::kNumPatterns; ++i)
        patternKeys[static_cast<size_t>(i)]->setEnabled(i < processor.numPatterns());
    osc1.repaint();
    resized();
    repaint();
}

void RubberDuckEditor::updatePatternKeys()
{
    const int rows = stepEditor.numRows();
    const int selected = stepEditor.getPatternIndex();
    const bool running = processor.isSequencerRunning();
    const int playing = running ? processor.getPlayingPattern() : -1;

    // Waiting patterns: a selection not in effect yet, the other half of a
    // 32-step pair, or the next pattern of the v1.02 Sequence range.
    if (!running || playing == pendingPattern || (rows == 2 && (playing & ~1) == (pendingPattern & ~1)))
        pendingPattern = -1;
    int next = -1;
    if (running) {
        if (rows == 2 && (playing & ~1) == (selected & ~1))
            next = playing ^ 1;
        else if (range.isActive() && playMode() == 1) {
            const int from = static_cast<int>(processor.apvts.getRawParameterValue("rangestart")->load());
            const int to = static_cast<int>(processor.apvts.getRawParameterValue("rangeend")->load());
            if (playing >= from && playing <= to)
                next = playing < to ? playing + 1 : from;
        }
    }
    for (int i = 0; i < rd::kNumPatterns; ++i) {
        const bool isSelected = rows == 2 ? (i & ~1) == (selected & ~1) : i == selected;
        const bool waiting = i != playing && (i == next || i == pendingPattern);
        patternKeys[static_cast<size_t>(i)]->setState(
            isSelected, i == playing ? PatternKey::Led::Playing : (waiting ? PatternKey::Led::Pending : PatternKey::Led::Off));
    }
}

void RubberDuckEditor::timerCallback()
{
    if (const auto preference = rd::ui::preferredTheme(); preference != seenPreference) {
        // Changed from another instance.
        seenPreference = preference;
        setTheme(preference, false);
    }
    showMachineFile();
    if (processor.hasOwnTransport()) {
        showPlaying();
        tempo.setKnob(processor.getTempoKnob());
        // Once shown, so that the space bar works before any click.
        if (!focusTaken && isShowing()) {
            focusTaken = true;
            grabKeyboardFocus();
        }
    }
    const int engineKind = static_cast<int>(processor.getEngineKind());
    const int mode = playMode();
    if (engineKind != shownEngine || mode != shownPlayMode) {
        shownEngine = engineKind;
        shownPlayMode = mode;
        updateEngineControls();
    }
    const int rows = stepEditor.numRows();
    if (rows != shownRows) {
        shownRows = rows;
        setSize(kWidth, editorHeight());
    }
    const int rangeValue = static_cast<int>(processor.apvts.getRawParameterValue("rangestart")->load()) * 16
                           + static_cast<int>(processor.apvts.getRawParameterValue("rangeend")->load());
    if (rangeValue != shownRange) {
        shownRange = rangeValue;
        repaint(kPatternX, kPatternY + kPatternH - 6, rd::kNumPatterns * kPatternPitch, 8);
    }
    undoButton.setEnabled(stepEditor.canUndo());
    redoButton.setEnabled(stepEditor.canRedo());
    pasteButton.setEnabled(stepEditor.canPaste());
    clearButton.setEnabled(stepEditor.canClear());
    if (clearArmedFor.isNotEmpty()
        && (!clearButton.isEnabled() || clearArmedFor != stepEditor.shownName()
            || juce::Time::getMillisecondCounter() - clearArmedAt > 3000))
        disarmClear();
    entryOctave.setText(juce::String(stepEditor.getEntryOctave()));

    const int selected = stepEditor.getPatternIndex();
    const int playing = processor.isSequencerRunning() ? processor.getPlayingPattern() : -1;
    if (selected != shownPattern && playing >= 0 && selected != playing)
        pendingPattern = selected;
    shownPattern = selected;
    shownPlaying = playing;
    updatePatternKeys();
}

void RubberDuckEditor::paint(juce::Graphics& g)
{
    const auto& t = lookAndFeel.theme();
    const bool light = t.kind == ThemeKind::Light;
    const int width = getWidth(), height = getHeight();
    g.fillAll(t.chassis);
    g.setColour(juce::Colours::white.withAlpha(light ? 0.6f : 0.06f));
    g.fillRect(0, 0, width, 1);
    if (light) {
        g.setColour(juce::Colours::black.withAlpha(0.12f));
        g.fillRect(0, height - 1, width, 1);
    }
    using L = juce::Line<float>;
    drawScrew(g, t, {5.0f, 5.0f}, L(3.2f, 4.6f, 8.8f, 7.4f));
    drawScrew(g, t, {static_cast<float>(width) - 17.0f, 5.0f}, L(4.4f, 3.2f, 7.6f, 8.8f));
    drawScrew(g, t, {5.0f, static_cast<float>(height) - 17.0f}, L(3.0f, 6.8f, 9.0f, 5.2f));
    drawScrew(g, t, {static_cast<float>(width) - 17.0f, static_cast<float>(height) - 17.0f}, L(5.0f, 3.0f, 7.0f, 9.0f));

    // Name, on the baseline of the header's centre line.
    {
        const auto name = nameFont();
        const float baseline = static_cast<float>(kHeaderY + kHeaderH / 2) + 5.0f;
        g.setColour(light ? t.ink : t.acid);
        g.setFont(name);
        g.drawSingleLineText("RUBDUCK", nameX, juce::roundToInt(baseline));
    }

    const int patternCentre = kPatternY + kPatternH / 2;
    const auto caption = rd::ui::font(Face::Ui700, 11.0f, 0.2f);
    g.setColour(t.label);
    g.setFont(caption);
    g.drawText("PATTERN", kLeft, patternCentre - 8, 66, 16, juce::Justification::centredLeft, false);

    // SEQ RANGE, with the mode under it when the range plays.
    const bool rangeOn = range.isActive();
    g.setColour(rangeOn ? t.ink : t.dim);
    g.drawText("SEQ RANGE", rangeCaption.withHeight(14).withTrimmedRight(-8), juce::Justification::centredLeft, false);
    if (rangeOn) {
        g.setColour(t.label);
        g.setFont(rd::ui::font(Face::Ui600, 9.5f, 0.12f));
        g.drawText(playMode() == 2 ? juce::CharPointer_UTF8("V1.02 \xc2\xb7 SHUFFLE") : juce::CharPointer_UTF8("V1.02 \xc2\xb7 SEQUENCE"),
                   rangeCaption.withTrimmedTop(15).withTrimmedRight(-8), juce::Justification::centredLeft, false);
        // A bar under the patterns of the range.
        const int from = static_cast<int>(processor.apvts.getRawParameterValue("rangestart")->load());
        const int to = static_cast<int>(processor.apvts.getRawParameterValue("rangeend")->load());
        const int a = std::min(from, to), b = std::max(from, to);
        const int y = patternKeys[0]->getBottom() + 4;
        g.setColour(t.ink);
        g.fillRect(kPatternX + a * kPatternPitch, y, (b - a) * kPatternPitch + 28, 3);
    }

    // Section frames, their titles notched into the top edge.
    const auto title = sectionTitleFont();
    for (const auto& s : sections) {
        g.setColour(t.line);
        g.drawRoundedRectangle(s.bounds.toFloat().reduced(0.5f), 8.0f, 1.0f);
        const int w = cssWidth(title, s.text) + 12;
        const juce::Rectangle<int> notch(s.bounds.getX() + 15, s.bounds.getY() - 7, w, 14);
        g.setColour(t.chassis);
        g.fillRect(notch);
        g.setColour(t.label);
        g.setFont(title);
        g.drawText(s.text, notch.withTrimmedLeft(6), juce::Justification::centredLeft, false);
    }
    g.setColour(t.line);
    for (const auto& r : separators)
        g.fillRect(r);

    for (const auto& c : captions) {
        g.setColour(c.strong ? t.ink : t.label);
        g.setFont(c.strong ? rd::ui::font(Face::Ui700, 11.5f, 0.1f) : rd::ui::font(Face::Ui700, 10.5f, 0.16f));
        g.drawText(c.text, c.bounds.withTrimmedRight(-8), juce::Justification::centredLeft, false);
    }
}

void RubberDuckEditor::resized()
{
    const int dy = StepEditor::heightFor(stepEditor.numRows()) - StepEditor::heightFor(1);
    sections.clear();
    captions.clear();
    separators.clear();

    // Header: duck, name, preset ... (standalone: tempo) configuration VFD, LOAD, SAVE.
    const int headerCentre = kHeaderY + kHeaderH / 2;
    duck.setBounds(kLeft, headerCentre - 16, 33, 32);
    nameX = duck.getRight() + 14;
    int right = kRight;
    for (auto* b : {&saveButton, &loadButton}) {
        const int w = keyWidth(lookAndFeel, *b, 0, 12);
        b->setBounds(right - w, headerCentre - 13, w, 26);
        right -= w + 14;
    }
    const int configWidth = config.getIdealWidth();
    config.setBounds(right - configWidth, headerCentre - 17, configWidth, 34);
    right = config.getX() - 14;
    if (processor.hasOwnTransport()) {
        const int tw = tempo.getIdealWidth();
        tempo.setBounds(right - tw, headerCentre - 17, tw, 34);
        right = tempo.getX() - 14;
    }
    {
        const int x = nameX + cssWidth(nameFont(), "RUBDUCK") + 18;
        preset.setBounds(x, headerCentre - 17, std::max(0, right - x), 34);
    }

    // Pattern row: 16 keys, SEQ RANGE, DRAG MIDI.
    const int patternCentre = kPatternY + kPatternH / 2;
    for (int i = 0; i < rd::kNumPatterns; ++i)
        patternKeys[static_cast<size_t>(i)]->setBounds(kPatternX + i * kPatternPitch, kPatternY + 4, 28, 36);
    {
        const int x = kPatternX + rd::kNumPatterns * kPatternPitch - 6 + 22;
        const auto caption = rd::ui::font(Face::Ui700, 11.0f, 0.2f);
        int w = cssWidth(caption, "SEQ RANGE");
        int h = 14;
        if (range.isActive()) {
            w = std::max(w, cssWidth(rd::ui::font(Face::Ui600, 9.5f, 0.12f), juce::CharPointer_UTF8("V1.02 \xc2\xb7 SEQUENCE")));
            h = 27;
        }
        rangeCaption = {x, patternCentre - h / 2, w, h};
        range.setBounds(x + w + 8, patternCentre - 12, 66, 24);
        const int mw = midiButton.getIdealWidth();
        midiButton.setBounds(kRight - mw, patternCentre - 15, mw, 30);
    }

    // Sequencer frame: toolbar, then the grid.
    const juce::Rectangle<int> sequencer(kLeft, kSequencerY, kRight - kLeft, kSequencerH + dy);
    sections.push_back({"SEQUENCER", sequencer});
    {
        const int sw = stepsSwitch.getIdealWidth();
        stepsSwitch.setBounds(sequencer.getRight() - 15 - sw, sequencer.getY() - 10, sw, 20);
    }
    const auto inner = sequencer.reduced(15, 0).withTrimmedTop(15).withTrimmedBottom(11);
    {
        auto bar = inner.withHeight(24);
        const auto captionFont = rd::ui::font(Face::Ui700, 10.5f, 0.16f);
        auto addCaption = [&](const char* text) {
            const int w = cssWidth(captionFont, text);
            captions.push_back({text, bar.removeFromLeft(w)});
            bar.removeFromLeft(4 + 4);
        };
        auto place = [&](juce::TextButton& b, int minWidth, int padding) {
            const int w = keyWidth(lookAndFeel, b, minWidth, padding);
            b.setBounds(bar.removeFromLeft(w).withSizeKeepingCentre(w, 22));
            bar.removeFromLeft(4);
        };
        addCaption("SHIFT");
        place(shiftLeft, 28, 7);
        place(shiftRight, 28, 7);
        bar.removeFromLeft(18 + 4);
        addCaption("TRANSPOSE");
        for (auto* b : {&octaveDown, &semitoneDown, &semitoneUp, &octaveUp})
            place(*b, 30, 7);
        bar.removeFromLeft(18 + 4);
        addCaption("EDIT");
        for (auto* b : {&undoButton, &redoButton, &copyButton, &pasteButton})
            place(*b, 0, 9);
        // Wide enough for both of its texts.
        place(clearButton, cssWidth(lookAndFeel.getTextButtonFont(clearButton, 22), "SURE?") + 2 * 9, 9);

        // KEY OCT - [4] +, from the right.
        auto placeRight = [&](juce::Component& c, int w) {
            c.setBounds(bar.removeFromRight(w).withSizeKeepingCentre(w, 22));
            bar.removeFromRight(4);
        };
        placeRight(entryOctaveUp, keyWidth(lookAndFeel, entryOctaveUp, 24, 6));
        placeRight(entryOctave, 26);
        placeRight(entryOctaveDown, keyWidth(lookAndFeel, entryOctaveDown, 24, 6));
        bar.removeFromRight(4);
        const int w = cssWidth(captionFont, "KEY OCT");
        captions.push_back({"KEY OCT", bar.removeFromRight(w)});
    }
    stepEditor.setBounds(inner.getX(), inner.getY() + 24 + 10, inner.getWidth(), StepEditor::heightFor(stepEditor.numRows()));

    // A knob column, centred vertically in a section's inner area.
    auto placeKnob = [this](const char* id, int x, juce::Rectangle<int> area) {
        auto& k = knob(id);
        const auto size = RdKnob::sizeFor(k.isBig());
        k.setBounds(x, area.getY() + (area.getHeight() - size.getHeight()) / 2, size.getWidth(), size.getHeight());
        return x + size.getWidth();
    };
    auto separator = [this](int x, juce::Rectangle<int> area) {
        separators.push_back({x + 8, area.getY() + (area.getHeight() - 96) / 2, 1, 96});
        return x + 17;
    };
    // Inside the 1 px border: 8 px on top, 12 on the sides, 6 below.
    auto content = [](juce::Rectangle<int> r) { return r.reduced(13, 0).withTrimmedTop(9).withTrimmedBottom(7); };

    // Row 1: Filter, Envelope.
    const int row1 = kRow1Y + dy;
    const juce::Rectangle<int> filter(kLeft, row1, 620, kRow1H), envelope(kLeft + 632, row1, 332, kRow1H);
    sections.push_back({"FILTER", filter});
    sections.push_back({"ENVELOPE", envelope});
    {
        const auto area = content(filter);
        int x = area.getX();
        for (const char* id : {"cutoff", "resonance", "modulation"})
            x = placeKnob(id, x, area);
        x = separator(x, area);
        for (const char* id : {"squelch", "drive"})
            x = placeKnob(id, x, area);
        int y = area.getY() + (area.getHeight() - (3 * 24 + 2 * 7)) / 2;
        for (const char* id : {"modification", "tweak", "tweet"}) {
            toggle(id).setBounds(area.getRight() - 74, y, 74, 24);
            y += 24 + 7;
        }
    }
    {
        const auto area = content(envelope);
        int x = area.getX() + (area.getWidth() - 3 * 96) / 2;
        for (const char* id : {"attack", "decay", "accent"})
            x = placeKnob(id, x, area);
    }

    // Row 2: Oscillators, Effects, Master.
    const int row2 = kRow2Y + dy;
    const juce::Rectangle<int> osc(kLeft, row2, 356, kRow2H), fx(kLeft + 368, row2, 474, kRow2H),
        master(kLeft + 854, row2, 110, kRow2H);
    sections.push_back({"OSCILLATORS", osc});
    sections.push_back({"EFFECTS", fx});
    sections.push_back({"MASTER", master});
    {
        const auto area = content(osc);
        const int y1 = area.getY() + (area.getHeight() - (24 + 8 + 24)) / 2, y2 = y1 + 32;
        const int x = area.getX();
        captions.push_back({"OSC 1", {x, y1, 38, 24}, true});
        captions.push_back({"OSC 2", {x, y2, 38, 24}, true});
        osc1.setBounds(x + 46, y1, 104, 24);
        osc2.setBounds(x + 46, y2, 64, 24);
        toggle("osc2double").setBounds(x + 46 + 64 + 8, y2, 34, 24);
        placeKnob("tune", area.getRight() - 64, area);
        placeKnob("mix", area.getRight() - 64 - 6 - 96, area);
    }
    {
        // Separator in the middle; delay and distortion each centred in the
        // space between the frame's border and the separator.
        const auto area = content(fx);
        const int line = fx.getCentreX() - 1;
        separator(line - 8, area);
        auto placeGroup = [&](std::initializer_list<const char*> ids, int from, int to) {
            int width = 0;
            for (const char* id : ids)
                width += RdKnob::sizeFor(knob(id).isBig()).getWidth();
            int x = from + (to - from - width) / 2;
            for (const char* id : ids)
                x = placeKnob(id, x, area);
        };
        placeGroup({"delaytime", "delaygain", "delayfeedback"}, fx.getX() + 1, line);
        placeGroup({"distgain", "distdrive"}, line + 1, fx.getRight() - 1);
    }
    {
        const auto area = content(master);
        const int x = area.getX() + (area.getWidth() - 64) / 2;
        if (processor.hasOwnTransport()) {
            // The volume, with PLAY under it.
            const auto size = RdKnob::sizeFor(false);
            constexpr int keyH = 30, gap = 10;
            const int y = area.getY() + (area.getHeight() - (size.getHeight() + gap + keyH)) / 2;
            knob("volume").setBounds(x, y, size.getWidth(), size.getHeight());
            playButton.setBounds(x, y + size.getHeight() + gap, size.getWidth(), keyH);
        } else {
            placeKnob("volume", x, area);
        }
    }
}
