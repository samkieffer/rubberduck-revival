#include "Widgets.h"

#include "RdLookAndFeel.h"

using rd::ui::Face;

namespace {

constexpr float kPi = juce::MathConstants<float>::pi;

juce::Font vfdFont()
{
    return rd::ui::font(Face::Vfd, 13.0f);
}

// Shows the choices of `param` in a menu, then calls `chosen` with the index picked.
void showChoiceMenu(juce::Component& target, const juce::StringArray& items, int current,
                    const std::function<bool(int)>& enabled, std::function<void(int)> chosen)
{
    juce::PopupMenu menu;
    for (int i = 0; i < items.size(); ++i)
        menu.addItem(i + 1, items[i], !enabled || enabled(i), i == current);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&target).withMinimumWidth(target.getWidth()),
                       [callback = std::move(chosen)](int result) {
                           if (result > 0)
                               callback(result - 1);
                       });
}

} // namespace

// ---------------------------------------------------------------------------

RdKnob::RdKnob(const juce::String& label, bool big, bool bipolar)
{
    getProperties().set("rd.label", label.toUpperCase());
    getProperties().set("rd.big", big);
    getProperties().set("rd.bipolar", bipolar);
    setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    setRotaryParameters(1.25f * kPi, 2.75f * kPi, true);
    setTextBoxStyle(juce::Slider::TextBoxBelow, false, 52, 14);
}

void RdKnob::mouseEnter(const juce::MouseEvent& e)
{
    Slider::mouseEnter(e);
    updateValueBox();
}

void RdKnob::mouseExit(const juce::MouseEvent& e)
{
    Slider::mouseExit(e);
    updateValueBox();
}

void RdKnob::enablementChanged()
{
    Slider::enablementChanged();
    updateValueBox();
}

void RdKnob::lookAndFeelChanged()
{
    // Recreates the value box.
    Slider::lookAndFeelChanged();
    updateValueBox();
}

void RdKnob::updateValueBox()
{
    auto* lf = RdLookAndFeel::of(*this);
    for (auto* c : getChildren())
        if (auto* box = dynamic_cast<juce::Label*>(c)) {
            box->setVisible(isEnabled());
            if (lf != nullptr)
                box->setColour(juce::Label::textColourId,
                               isMouseOverOrDragging(true) ? lf->theme().valueHover : lf->theme().value);
        }
}

// ---------------------------------------------------------------------------

LedKey::LedKey(const juce::String& text, bool isCompact) : juce::Button(text), compact(isCompact)
{
    setClickingTogglesState(true);
    setWantsKeyboardFocus(false);
}

void LedKey::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    auto* lf = RdLookAndFeel::of(*this);
    if (lf == nullptr)
        return;
    const auto& t = lf->theme();
    const bool on = isEnabled();
    const auto r = getLocalBounds().toFloat();
    lf->drawKey(g, r, 3.0f, on, highlighted, down);
    const auto face = r.withTrimmedTop(down && on ? 2.0f : 1.0f).withTrimmedBottom(down && on ? 1.0f : 2.0f);
    const float pad = compact ? 6.0f : 8.0f, gap = compact ? 4.0f : 7.0f;
    const bool lit = on && getToggleState();
    lf->drawLed(g, {face.getX() + pad + 3.0f, face.getCentreY()}, 6.0f, lit ? t.acid : (on ? t.accOff : t.ledOffDisabled),
                lit);
    g.setColour(on ? t.ink : t.dim);
    g.setFont(rd::ui::font(Face::Ui700, 12.0f, compact ? 0.0f : 0.12f));
    g.drawText(getButtonText(), face.withTrimmedLeft(pad + 6.0f + gap), juce::Justification::centredLeft, false);
}

// ---------------------------------------------------------------------------

PatternKey::PatternKey(const juce::String& name) : juce::Button(name)
{
    setWantsKeyboardFocus(false);
}

void PatternKey::setState(bool isSelected, Led newLed)
{
    if (isSelected == selected && newLed == led)
        return;
    selected = isSelected;
    led = newLed;
    repaint();
}

void PatternKey::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    auto* lf = RdLookAndFeel::of(*this);
    if (lf == nullptr)
        return;
    const auto& t = lf->theme();
    const auto w = static_cast<float>(getWidth());

    // LED, 6 px, above the key.
    const juce::Point<float> ledCentre(w * 0.5f, 3.0f);
    if (led == Led::Playing)
        lf->drawLed(g, ledCentre, 6.0f, t.play, true);
    else if (led == Led::Pending) {
        g.setColour(t.play.withAlpha(0.47f));
        g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre(ledCentre));
    } else
        lf->drawLed(g, ledCentre, 6.0f, t.playOff, false);

    const auto key = juce::Rectangle<float>(26.0f, 26.0f).withCentre({w * 0.5f, 10.0f + 13.0f});
    const bool available = isEnabled();
    const bool pressed = down && available;
    juce::Colour face = t.key, hi = t.keyHi, text = t.ink;
    if (!available) {
        face = t.patNaCap;
        hi = t.patNaHi;
        text = t.patNaText;
    } else if (selected) {
        face = t.acid;
        hi = t.patSelHi;
        text = t.onAcid;
    } else if (highlighted) {
        hi = hi.interpolatedWith(juce::Colours::white, 0.3f);
    }
    lf->drawKey(g, key, 13.0f, face, hi, t.keyLo, pressed);
    g.setColour(text);
    g.setFont(rd::ui::font(Face::Vfd, 12.0f));
    g.drawText(getButtonText(), key.withTrimmedTop(pressed ? 2.0f : 1.0f).withTrimmedBottom(pressed ? 1.0f : 2.0f),
               juce::Justification::centred, false);
}

// ---------------------------------------------------------------------------

StepsSwitch::StepsSwitch() : juce::Button("Steps")
{
    setClickingTogglesState(true);
    setWantsKeyboardFocus(false);
}

int StepsSwitch::getIdealWidth() const
{
    const auto caption = rd::ui::font(Face::Ui700, 11.0f, 0.22f);
    const auto number = rd::ui::font(Face::Ui700, 13.0f);
    return static_cast<int>(std::ceil(6 + rd::ui::textWidth(caption, "STEPS") + 2 + 6 + rd::ui::textWidth(number, "16")
                                      + 6 + 36 + 6 + rd::ui::textWidth(number, "32") + 6));
}

void StepsSwitch::paintButton(juce::Graphics& g, bool, bool)
{
    auto* lf = RdLookAndFeel::of(*this);
    if (lf == nullptr)
        return;
    const auto& t = lf->theme();
    const bool on32 = getToggleState();
    auto r = getLocalBounds().toFloat();
    // Notched into the frame: hide the border line behind.
    g.setColour(t.chassis);
    g.fillRect(r);
    r.reduce(6.0f, 0.0f);

    const auto caption = rd::ui::font(Face::Ui700, 11.0f, 0.22f);
    const auto number = rd::ui::font(Face::Ui700, 13.0f);
    g.setColour(t.label);
    g.setFont(caption);
    // Text rects get some slack: drawText drops the glyphs that do not fit.
    g.drawText("STEPS", r.removeFromLeft(rd::ui::textWidth(caption, "STEPS")).withTrimmedRight(-4.0f),
               juce::Justification::centredLeft, false);
    r.removeFromLeft(2.0f + 6.0f);
    g.setFont(number);
    g.setColour(on32 ? t.switchThumb : t.ink);
    g.drawText("16", r.removeFromLeft(rd::ui::textWidth(number, "16")).expanded(3.0f, 0.0f), juce::Justification::centred,
               false);
    r.removeFromLeft(6.0f);
    const auto track = r.removeFromLeft(36.0f).withSizeKeepingCentre(36.0f, 18.0f);
    r.removeFromLeft(6.0f);
    g.setColour(on32 ? t.ink : t.switchThumb);
    g.drawText("32", r.removeFromLeft(rd::ui::textWidth(number, "32")).expanded(3.0f, 0.0f), juce::Justification::centred,
               false);

    g.setColour(t.switchTrack);
    g.fillRoundedRectangle(track, 3.0f);
    {
        juce::Graphics::ScopedSaveState state(g);
        juce::Path clip;
        clip.addRoundedRectangle(track, 3.0f);
        g.reduceClipRegion(clip);
        g.setGradientFill(juce::ColourGradient(juce::Colours::black.withAlpha(0.9f), 0.0f, track.getY(),
                                               juce::Colours::transparentBlack, 0.0f, track.getY() + 3.0f, false));
        g.fillRect(track.withHeight(3.0f));
    }
    const auto thumb = juce::Rectangle<float>(track.getX() + (on32 ? 19.0f : 2.0f), track.getY() + 3.0f, 15.0f, 12.0f);
    g.setColour(t.switchThumbHi);
    g.fillRoundedRectangle(thumb, 2.0f);
    g.setColour(t.switchThumb);
    g.fillRoundedRectangle(thumb.withTrimmedTop(1.0f), 2.0f);
}

// ---------------------------------------------------------------------------

void VfdSelector::addSegment(juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
                             const juce::String& prefix, const juce::StringArray& labels, const juce::String& tooltip)
{
    auto s = std::make_unique<Segment>();
    s->prefix = prefix;
    s->labels = labels;
    s->tooltip = tooltip;
    s->param = dynamic_cast<juce::AudioParameterChoice*>(state.getParameter(paramId));
    jassert(s->param != nullptr);
    auto* segment = s.get();
    s->attachment = std::make_unique<juce::ParameterAttachment>(*s->param, [this, segment](float value) {
        segment->index = juce::roundToInt(value);
        // The window follows the width of its texts.
        if (auto* parent = getParentComponent())
            parent->resized();
        resized();
        repaint();
    });
    segments.push_back(std::move(s));
    segment->attachment->sendInitialUpdate();
}

float VfdSelector::segmentWidth(const Segment& s) const
{
    return rd::ui::textWidth(vfdFont(), s.prefix + s.labels[juce::jlimit(0, s.labels.size() - 1, s.index)]) + 12.0f;
}

int VfdSelector::getIdealWidth() const
{
    float w = 12.0f;
    for (const auto& s : segments)
        w += segmentWidth(*s);
    const auto n = static_cast<float>(segments.size());
    w += (n - 1.0f) * (rd::ui::textWidth(vfdFont(), juce::CharPointer_UTF8("\xc2\xb7")) + 4.0f);
    return static_cast<int>(std::ceil(w));
}

void VfdSelector::resized()
{
    float x = 6.0f;
    const float dot = rd::ui::textWidth(vfdFont(), juce::CharPointer_UTF8("\xc2\xb7")) + 4.0f;
    for (auto& s : segments) {
        const float w = segmentWidth(*s);
        s->bounds = {x, 4.0f, w, static_cast<float>(getHeight()) - 8.0f};
        x += w + dot;
    }
}

void VfdSelector::paint(juce::Graphics& g)
{
    auto* lf = RdLookAndFeel::of(*this);
    if (lf == nullptr)
        return;
    const auto& t = lf->theme();
    const auto font = vfdFont();
    lf->drawVfdWindow(g, getLocalBounds().toFloat(), 4.0f);
    const auto dot = juce::String(juce::CharPointer_UTF8("\xc2\xb7"));
    for (size_t i = 0; i < segments.size(); ++i) {
        const auto& s = *segments[i];
        auto r = s.bounds.reduced(6.0f, 0.0f);
        if (s.prefix.isNotEmpty())
            lf->drawGlowText(g, s.prefix, font, r.removeFromLeft(rd::ui::textWidth(font, s.prefix)),
                             t.acid.withMultipliedAlpha(0.45f), juce::Justification::centredLeft, true);
        lf->drawGlowText(g, s.labels[juce::jlimit(0, s.labels.size() - 1, s.index)], font, r, t.acid,
                         juce::Justification::centredLeft, true);
        if (i + 1 < segments.size())
            lf->drawGlowText(g, dot, font, {s.bounds.getRight(), s.bounds.getY(), rd::ui::textWidth(font, dot) + 4.0f,
                                            s.bounds.getHeight()},
                             t.acid.withMultipliedAlpha(0.3f), juce::Justification::centred, false);
    }
}

int VfdSelector::segmentAt(juce::Point<float> p) const
{
    for (size_t i = 0; i < segments.size(); ++i)
        if (segments[i]->bounds.expanded(2.0f, 4.0f).contains(p))
            return static_cast<int>(i);
    return -1;
}

void VfdSelector::mouseMove(const juce::MouseEvent& e)
{
    const int now = segmentAt(e.position);
    if (now != hovered) {
        hovered = now;
        setMouseCursor(now >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }
}

void VfdSelector::mouseExit(const juce::MouseEvent&)
{
    hovered = -1;
}

juce::String VfdSelector::getTooltip()
{
    return hovered >= 0 ? segments[static_cast<size_t>(hovered)]->tooltip : juce::String();
}

void VfdSelector::mouseDown(const juce::MouseEvent& e)
{
    const int i = segmentAt(e.position);
    if (i < 0)
        return;
    auto* s = segments[static_cast<size_t>(i)].get();
    showChoiceMenu(*this, s->param->choices, s->index, {},
                   [s](int index) { s->attachment->setValueAsCompleteGesture(static_cast<float>(index)); });
}

// ---------------------------------------------------------------------------

VfdChoice::VfdChoice(juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
                     const juce::StringArray& choiceLabels)
    : labels(choiceLabels)
{
    param = dynamic_cast<juce::AudioParameterChoice*>(state.getParameter(paramId));
    jassert(param != nullptr);
    attachment = std::make_unique<juce::ParameterAttachment>(*param, [this](float value) {
        index = juce::roundToInt(value);
        repaint();
    });
    attachment->sendInitialUpdate();
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void VfdChoice::select(int newIndex)
{
    attachment->setValueAsCompleteGesture(static_cast<float>(newIndex));
}

void VfdChoice::paint(juce::Graphics& g)
{
    auto* lf = RdLookAndFeel::of(*this);
    if (lf == nullptr)
        return;
    const auto& t = lf->theme();
    const auto r = getLocalBounds().toFloat();
    lf->drawVfdWindow(g, r, 3.0f);
    const auto arrow = t.acid.withMultipliedAlpha(0.45f);
    RdLookAndFeel::drawTriangle(g, {7.0f, r.getCentreY() - 3.5f, 5.0f, 7.0f}, false, arrow);
    RdLookAndFeel::drawTriangle(g, {r.getRight() - 12.0f, r.getCentreY() - 3.5f, 5.0f, 7.0f}, true, arrow);
    lf->drawGlowText(g, labels[juce::jlimit(0, labels.size() - 1, index)], vfdFont(), r.reduced(12.0f, 0.0f), t.acid,
                     juce::Justification::centred, true);
}

void VfdChoice::mouseDown(const juce::MouseEvent& e)
{
    const int n = labels.size();
    auto enabled = [this](int i) { return !isChoiceEnabled || isChoiceEnabled(i); };
    const float x = e.position.x;
    if (x < 16.0f || x > static_cast<float>(getWidth()) - 16.0f) {
        const int step = x < 16.0f ? -1 : 1;
        for (int i = 1; i < n; ++i) {
            const int candidate = ((index + step * i) % n + n) % n;
            if (enabled(candidate)) {
                select(candidate);
                return;
            }
        }
        return;
    }
    showChoiceMenu(*this, param->choices, index, enabled, [this](int i) { select(i); });
}

// ---------------------------------------------------------------------------

RangeVfd::RangeVfd(juce::AudioProcessorValueTreeState& state)
{
    const char* ids[2] = {"rangestart", "rangeend"};
    for (int i = 0; i < 2; ++i) {
        params[i] = dynamic_cast<juce::AudioParameterChoice*>(state.getParameter(ids[i]));
        jassert(params[i] != nullptr);
        attachments[i] = std::make_unique<juce::ParameterAttachment>(*params[i], [this, i](float value) {
            values[i] = juce::roundToInt(value);
            repaint();
        });
        attachments[i]->sendInitialUpdate();
    }
}

void RangeVfd::setActive(bool isActive)
{
    if (active == isActive)
        return;
    active = isActive;
    setMouseCursor(active ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    setTooltip(active ? "Patterns played by the Sequence and Shuffle modes: click a number to change it"
                      : "Engine v1.02, Sequence or Shuffle only");
    repaint();
}

void RangeVfd::paint(juce::Graphics& g)
{
    auto* lf = RdLookAndFeel::of(*this);
    if (lf == nullptr)
        return;
    const auto& t = lf->theme();
    const auto r = getLocalBounds().toFloat();
    lf->drawVfdWindow(g, r, 3.0f);
    const auto colour = active ? t.acid : t.acid.withMultipliedAlpha(0.22f);
    const auto font = vfdFont();
    const float cx = r.getCentreX(), cy = r.getCentreY();
    for (int i = 0; i < 2; ++i) {
        const auto name = params[i]->choices[juce::jlimit(0, params[i]->choices.size() - 1, values[i])];
        lf->drawGlowText(g, name, font, {cx + (i == 0 ? -21.0f : 7.0f), r.getY(), 14.0f, r.getHeight()}, colour,
                         juce::Justification::centred, active);
    }
    // The arrow between the two numbers.
    g.setColour(colour);
    juce::Path arrow;
    arrow.startNewSubPath(cx - 4.5f, cy);
    arrow.lineTo(cx + 4.0f, cy);
    arrow.startNewSubPath(cx + 1.0f, cy - 3.0f);
    arrow.lineTo(cx + 4.5f, cy);
    arrow.lineTo(cx + 1.0f, cy + 3.0f);
    g.strokePath(arrow, juce::PathStrokeType(1.2f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
}

void RangeVfd::mouseDown(const juce::MouseEvent& e)
{
    if (!active)
        return;
    const int i = e.position.x < static_cast<float>(getWidth()) * 0.5f ? 0 : 1;
    showChoiceMenu(*this, params[i]->choices, values[i], {},
                   [this, i](int v) { attachments[i]->setValueAsCompleteGesture(static_cast<float>(v)); });
}

// ---------------------------------------------------------------------------

void VfdReadout::setText(const juce::String& newText)
{
    if (newText != text) {
        text = newText;
        repaint();
    }
}

void VfdReadout::paint(juce::Graphics& g)
{
    auto* lf = RdLookAndFeel::of(*this);
    if (lf == nullptr)
        return;
    lf->drawVfdWindow(g, getLocalBounds().toFloat(), 3.0f);
    lf->drawGlowText(g, text, vfdFont(), getLocalBounds().toFloat(), lf->theme().acid, juce::Justification::centred, true);
}

// ---------------------------------------------------------------------------

DuckBadge::DuckBadge()
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setTooltip("Theme");
}

void DuckBadge::paint(juce::Graphics& g)
{
    auto* lf = RdLookAndFeel::of(*this);
    if (lf == nullptr)
        return;
    const auto& t = lf->theme();
    // 11 x 7 pixels, 3 px each: # body, B beak, e eye.
    static const char* map[7] = {"...###.....", "..##e##....", "BB#####....", "...###....#",
                                 ".##########", ".#########.", "..#######.."};
    constexpr int px = 3;
    const int x0 = (getWidth() - 11 * px) / 2, y0 = (getHeight() - 7 * px) / 2;
    for (int y = 0; y < 7; ++y)
        for (int x = 0; x < 11; ++x) {
            const char c = map[y][x];
            if (c == '.')
                continue;
            // The eye stays dark: the chassis in the dark theme, ink in the light one.
            const auto eye = t.kind == rd::ui::ThemeKind::Light ? t.ink : t.chassis;
            g.setColour(c == '#' ? t.acid : (c == 'B' ? t.beak : eye));
            g.fillRect(x0 + x * px, y0 + y * px, px, px);
        }
}

void DuckBadge::mouseDown(const juce::MouseEvent&)
{
    if (onClick)
        onClick();
}
