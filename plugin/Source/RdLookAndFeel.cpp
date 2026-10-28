#include "RdLookAndFeel.h"

#include <cmath>

using rd::ui::Face;

namespace {

constexpr float kGlowAlpha = 0.42f;
constexpr int kGlowRadius = 6;

struct KnobGeometry {
    bool big;
    float size, ticksFrom, ticksTo, tickWidth, capRadius, faceRadius, shadowOffset, pointerFrom, pointerTo, pointerWidth,
        labelSize;
    int ticks;
    float labelHeight() const { return labelSize * 1.2f; }
    float valueY() const { return size + 1.0f + labelHeight() + 1.0f; }
};

KnobGeometry knobGeometry(const juce::Slider& s)
{
    if (s.getProperties()["rd.big"])
        return {true, 80.0f, 31.0f, 36.5f, 2.0f, 25.0f, 19.5f, 2.5f, 6.0f, 22.0f, 3.5f, 14.0f, 11};
    return {false, 46.0f, 17.5f, 21.0f, 1.6f, 13.5f, 0.0f, 1.5f, 3.0f, 11.0f, 2.0f, 11.5f, 9};
}

juce::Point<float> polar(juce::Point<float> centre, float radius, float angle)
{
    return {centre.x + radius * std::sin(angle), centre.y - radius * std::cos(angle)};
}

void strokeLine(juce::Graphics& g, juce::Point<float> a, juce::Point<float> b, float width)
{
    juce::Path p;
    p.startNewSubPath(a);
    p.lineTo(b);
    g.strokePath(p, juce::PathStrokeType(width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

} // namespace

RdLookAndFeel::RdLookAndFeel()
{
    setDefaultSansSerifTypeface(typefaces->get(Face::Ui600));
    setTheme(rd::ui::Theme::dark());
}

void RdLookAndFeel::setTheme(const rd::ui::Theme& theme)
{
    current = &theme;
    glowCache.clear();
    const auto& t = theme;
    const auto clear = juce::Colours::transparentBlack;
    setColour(juce::ResizableWindow::backgroundColourId, t.chassis);
    setColour(juce::Slider::textBoxTextColourId, t.value);
    setColour(juce::Slider::textBoxBackgroundColourId, clear);
    setColour(juce::Slider::textBoxOutlineColourId, clear);
    setColour(juce::Slider::textBoxHighlightColourId, t.acid.withAlpha(0.35f));
    setColour(juce::Label::textColourId, t.ink);
    setColour(juce::Label::textWhenEditingColourId, t.ink);
    setColour(juce::Label::backgroundWhenEditingColourId, clear);
    setColour(juce::Label::outlineWhenEditingColourId, clear);
    setColour(juce::TextEditor::backgroundColourId, clear);
    setColour(juce::TextEditor::textColourId, t.ink);
    setColour(juce::TextEditor::highlightColourId, t.acid.withAlpha(0.35f));
    setColour(juce::TextEditor::highlightedTextColourId, t.ink);
    setColour(juce::TextEditor::outlineColourId, clear);
    setColour(juce::TextEditor::focusedOutlineColourId, clear);
    setColour(juce::CaretComponent::caretColourId, t.acid);
    setColour(juce::TextButton::textColourOffId, t.ink);
    setColour(juce::TextButton::textColourOnId, t.ink);
    setColour(juce::PopupMenu::backgroundColourId, t.key);
    setColour(juce::PopupMenu::textColourId, t.ink);
    setColour(juce::PopupMenu::headerTextColourId, t.label);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, t.acid);
    setColour(juce::PopupMenu::highlightedTextColourId, t.onAcid);
    setColour(juce::TooltipWindow::backgroundColourId, t.key);
    setColour(juce::TooltipWindow::textColourId, t.ink);
    setColour(juce::TooltipWindow::outlineColourId, t.line);
}

void RdLookAndFeel::drawKey(juce::Graphics& g, juce::Rectangle<float> r, float radius, bool enabled, bool hover,
                            bool down) const
{
    const auto& t = theme();
    auto face = enabled ? t.key : t.keyOff;
    if (enabled && hover && !down)
        face = face.interpolatedWith(juce::Colours::white, 0.06f);
    drawKey(g, r, radius, face, enabled ? t.keyHi : t.keyOffHi, enabled ? t.keyLo : t.keyOffLo, down && enabled);
}

void RdLookAndFeel::drawKey(juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour face,
                            juce::Colour hi, juce::Colour lo, bool down) const
{
    // Up: 1 px light edge on top, 2 px dark edge below. Down: the other way round.
    const float top = down ? 2.0f : 1.0f, bottom = down ? 1.0f : 2.0f;
    g.setColour(down ? hi : lo);
    g.fillRoundedRectangle(r, radius);
    g.setColour(down ? lo : hi);
    g.fillRoundedRectangle(r.withTrimmedBottom(bottom), radius);
    g.setColour(face);
    g.fillRoundedRectangle(r.withTrimmedTop(top).withTrimmedBottom(bottom), std::max(0.0f, radius - 0.5f));
}

void RdLookAndFeel::drawVfdWindow(juce::Graphics& g, juce::Rectangle<float> r, float radius) const
{
    const auto& t = theme();
    g.setColour(t.vfdEdge);
    g.fillRoundedRectangle(r.translated(0.0f, 1.0f), radius);
    g.setColour(t.vfd);
    g.fillRoundedRectangle(r, radius);
    // Inner shadow along the top edge.
    juce::Graphics::ScopedSaveState state(g);
    juce::Path clip;
    clip.addRoundedRectangle(r, radius);
    g.reduceClipRegion(clip);
    g.setGradientFill(juce::ColourGradient(juce::Colours::black.withAlpha(0.8f), 0.0f, r.getY(),
                                           juce::Colours::transparentBlack, 0.0f, r.getY() + 3.5f, false));
    g.fillRect(r.withHeight(4.0f));
}

void RdLookAndFeel::drawGlowText(juce::Graphics& g, const juce::String& text, const juce::Font& font,
                                 juce::Rectangle<float> area, juce::Colour colour, juce::Justification justification,
                                 bool glow)
{
    if (text.isEmpty())
        return;
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(font, text, area.getX(), area.getY());
    glyphs.justifyGlyphs(0, glyphs.getNumGlyphs(), area.getX(), area.getY(), area.getWidth(), area.getHeight(),
                         justification);
    if (glow) {
        juce::Path path;
        glyphs.createPath(path);
        const auto bounds = path.getBounds();
        const auto key = text + "|" + juce::String(font.getHeight(), 2) + "|" + font.getTypefaceName() + "|"
                         + colour.toString();
        auto& image = glowCache[key];
        constexpr int margin = kGlowRadius * 2;
        if (image.isNull()) {
            if (glowCache.size() > 512) {
                glowCache.clear();
                return drawGlowText(g, text, font, area, colour, justification, glow);
            }
            image = juce::Image(juce::Image::ARGB, static_cast<int>(std::ceil(bounds.getWidth())) + 2 * margin,
                                static_cast<int>(std::ceil(bounds.getHeight())) + 2 * margin, true);
            juce::Graphics ig(image);
            path.applyTransform(juce::AffineTransform::translation(margin - bounds.getX(), margin - bounds.getY()));
            juce::DropShadow(colour.withMultipliedAlpha(kGlowAlpha), kGlowRadius, {}).drawForPath(ig, path);
        }
        g.drawImageAt(image, juce::roundToInt(bounds.getX()) - margin, juce::roundToInt(bounds.getY()) - margin);
    }
    g.setColour(colour);
    glyphs.draw(g);
}

void RdLookAndFeel::drawLed(juce::Graphics& g, juce::Point<float> centre, float diameter, juce::Colour colour,
                            bool lit, bool ring) const
{
    const float r = diameter * 0.5f;
    const auto circle = juce::Rectangle<float>(diameter, diameter).withCentre(centre);
    if (lit) {
        const float glow = r + static_cast<float>(kGlowRadius);
        juce::ColourGradient halo(colour.withMultipliedAlpha(0.45f), centre, colour.withAlpha(0.0f),
                                  centre.translated(glow, 0.0f), true);
        halo.addColour(r / glow, colour.withMultipliedAlpha(0.45f));
        g.setGradientFill(halo);
        g.fillEllipse(juce::Rectangle<float>(2 * glow, 2 * glow).withCentre(centre));
    }
    g.setColour(colour);
    g.fillEllipse(circle);
    if (lit && ring && theme().ledRing) {
        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.drawEllipse(circle.expanded(0.5f), 1.0f);
    } else if (!lit) {
        // Unlit LEDs sit in the panel: a little shade on their top half.
        g.setGradientFill(juce::ColourGradient(juce::Colours::black.withAlpha(0.45f), centre.x, circle.getY(),
                                               juce::Colours::transparentBlack, centre.x, centre.y, false));
        g.fillEllipse(circle);
    }
}

void RdLookAndFeel::drawTriangle(juce::Graphics& g, juce::Rectangle<float> r, bool right, juce::Colour colour)
{
    juce::Path p;
    if (right)
        p.addTriangle(r.getX(), r.getY(), r.getRight(), r.getCentreY(), r.getX(), r.getBottom());
    else
        p.addTriangle(r.getRight(), r.getY(), r.getX(), r.getCentreY(), r.getRight(), r.getBottom());
    g.setColour(colour);
    g.fillPath(p);
}

void RdLookAndFeel::drawRotarySlider(juce::Graphics& g, int, int, int, int, float sliderPos, float startAngle,
                                     float endAngle, juce::Slider& slider)
{
    const auto& t = theme();
    const auto k = knobGeometry(slider);
    const bool on = slider.isEnabled();
    const bool bipolar = slider.getProperties()["rd.bipolar"];
    const juce::Point<float> centre(static_cast<float>(slider.getWidth()) * 0.5f, k.size * 0.5f);

    for (int i = 0; i < k.ticks; ++i) {
        const float f = static_cast<float>(i) / static_cast<float>(k.ticks - 1);
        const float angle = startAngle + f * (endAngle - startAngle);
        const bool lit = bipolar ? f >= std::min(0.5f, sliderPos) - 0.001f && f <= std::max(0.5f, sliderPos) + 0.001f
                                 : f <= sliderPos + 0.001f;
        g.setColour(!on ? t.tickDisabled : (lit ? t.tickOn : t.tickOff));
        strokeLine(g, polar(centre, k.ticksFrom, angle), polar(centre, k.ticksTo, angle), k.tickWidth);
    }

    const auto cap = juce::Rectangle<float>(2 * k.capRadius, 2 * k.capRadius).withCentre(centre);
    g.setColour(on ? t.knobShadow : t.knobShadowOff);
    g.fillEllipse(cap.translated(0.0f, k.shadowOffset));
    g.setColour(!on ? t.offCap : (k.big ? t.bigCap : t.smallCap));
    g.fillEllipse(cap);
    if (!on || !k.big) {
        g.setColour(on ? t.smallStroke : t.offStroke);
        g.drawEllipse(cap.reduced(0.5f), 1.0f);
    }
    if (on && k.big) {
        g.setColour(t.bigFace);
        g.fillEllipse(juce::Rectangle<float>(2 * k.faceRadius, 2 * k.faceRadius).withCentre(centre));
    }
    if (on) {
        const float angle = startAngle + sliderPos * (endAngle - startAngle);
        g.setColour(k.big ? t.bigPtr : t.smallPtr);
        strokeLine(g, polar(centre, k.pointerFrom, angle), polar(centre, k.pointerTo, angle), k.pointerWidth);
    }

    const auto width = static_cast<float>(slider.getWidth());
    g.setColour(on ? t.ink : t.dim);
    g.setFont(rd::ui::font(Face::Ui700, k.labelSize, 0.1f));
    g.drawText(slider.getProperties()["rd.label"].toString(), juce::Rectangle<float>(0.0f, k.size + 1.0f, width, k.labelHeight()),
               juce::Justification::centred, false);

    if (!on) {
        // The value gives way to a badge: the selected engine has no such control.
        const juce::String badge("2.12 ONLY");
        const auto font = rd::ui::font(Face::Ui700, 9.5f, 0.08f);
        const float w = rd::ui::textWidth(font, badge) + 8.0f;
        const auto r = juce::Rectangle<float>(w, 14.0f).withCentre({width * 0.5f, k.valueY() + 7.0f});
        g.setColour(t.badgeLine);
        g.drawRoundedRectangle(r.reduced(0.5f), 2.0f, 1.0f);
        g.setColour(t.badgeText);
        g.setFont(font);
        g.drawText(badge, r.translated(0.5f, 0.0f), juce::Justification::centred, false);
    }
}

juce::Slider::SliderLayout RdLookAndFeel::getSliderLayout(juce::Slider& slider)
{
    if (!slider.getProperties().contains("rd.label"))
        return LookAndFeel_V4::getSliderLayout(slider);
    const auto k = knobGeometry(slider);
    juce::Slider::SliderLayout layout;
    layout.sliderBounds = slider.getLocalBounds();
    layout.textBoxBounds = juce::Rectangle<int>(52, 14).withCentre(
        {slider.getWidth() / 2, juce::roundToInt(k.valueY()) + 7});
    return layout;
}

juce::Label* RdLookAndFeel::createSliderTextBox(juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox(slider);
    label->setFont(rd::ui::font(Face::Vfd, 10.5f));
    label->setJustificationType(juce::Justification::centred);
    label->setBorderSize({});
    label->setMinimumHorizontalScale(1.0f);
    return label;
}

void RdLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                         bool highlighted, bool down)
{
    const float radius = button.getProperties().getWithDefault("rd.radius", 3.0f);
    drawKey(g, button.getLocalBounds().toFloat(), radius, button.isEnabled(), highlighted, down);
}

juce::Font RdLookAndFeel::getTextButtonFont(juce::TextButton& button, int)
{
    const auto& props = button.getProperties();
    const int weight = props.getWithDefault("rd.weight", 600);
    const float size = props.getWithDefault("rd.fontSize", 12.0f);
    const float tracking = props.getWithDefault("rd.tracking", 0.0f);
    return rd::ui::font(weight >= 700 ? Face::Ui700 : (weight >= 600 ? Face::Ui600 : Face::Ui500), size, tracking);
}

void RdLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool down)
{
    const auto& t = theme();
    const auto colour = button.isEnabled() ? t.ink : t.dim;
    // The face lies between a 1 px top edge and a 2 px bottom edge.
    auto face = button.getLocalBounds().toFloat().withTrimmedTop(down ? 2.0f : 1.0f).withTrimmedBottom(down ? 1.0f : 2.0f);
    const auto icon = button.getProperties()["rd.icon"].toString();
    if (icon.isNotEmpty()) {
        drawTriangle(g, juce::Rectangle<float>(7.0f, 8.0f).withCentre(face.getCentre()), icon == "right", colour);
        return;
    }
    g.setColour(colour);
    g.setFont(getTextButtonFont(button, button.getHeight()));
    g.drawText(button.getButtonText(), face, juce::Justification::centred, false);
}

void RdLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height)
{
    g.fillAll(findColour(juce::PopupMenu::backgroundColourId));
    g.setColour(theme().line);
    g.drawRect(0, 0, width, height);
}

juce::Font RdLookAndFeel::getPopupMenuFont()
{
    return rd::ui::font(Face::Ui600, 15.0f, 0.04f);
}

juce::TextLayout RdLookAndFeel::tooltipLayout(const juce::String& text) const
{
    juce::AttributedString s;
    s.setJustification(juce::Justification::centredLeft);
    s.append(text, rd::ui::font(Face::Ui500, 14.0f, 0.02f), theme().ink);
    juce::TextLayout layout;
    layout.createLayoutWithBalancedLineLengths(s, 420.0f);
    return layout;
}

juce::Rectangle<int> RdLookAndFeel::getTooltipBounds(const juce::String& tipText, juce::Point<int> screenPos,
                                                     juce::Rectangle<int> parentArea)
{
    const auto layout = tooltipLayout(tipText);
    const int w = static_cast<int>(std::ceil(layout.getWidth())) + 18;
    const int h = static_cast<int>(std::ceil(layout.getHeight())) + 10;
    return juce::Rectangle<int>(screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6, w, h)
        .constrainedWithin(parentArea);
}

void RdLookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto& t = theme();
    const auto r = juce::Rectangle<int>(width, height).toFloat();
    g.setColour(t.key);
    g.fillRoundedRectangle(r, 4.0f);
    g.setColour(t.line);
    g.drawRoundedRectangle(r.reduced(0.5f), 4.0f, 1.0f);
    tooltipLayout(text).draw(g, r.reduced(9.0f, 5.0f));
}
