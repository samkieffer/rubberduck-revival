#include "Theme.h"

#include <atomic>

#include "RdFonts.h"

namespace rd::ui {

namespace {

juce::Colour rgb(juce::uint32 c)
{
    return juce::Colour(0xff000000 | c);
}

juce::PropertiesFile::Options settingsOptions()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "Rubber Duck Revival";
    o.folderName = "Rubber Duck Revival";
    o.filenameSuffix = ".settings";
    o.osxLibrarySubFolder = "Application Support";
    return o;
}

// -1 until the settings file has been read.
std::atomic<int> cachedTheme{-1};

} // namespace

const Theme& Theme::dark()
{
    static const Theme t = [] {
        Theme d{};
        d.kind = ThemeKind::Dark;
        d.chassis = rgb(0x1C1B19);
        d.ink = rgb(0xECE6D6);
        d.label = rgb(0xA29B8C);
        d.dim = rgb(0x5C574F);
        d.line = rgb(0x3B3833);
        d.ruler = rgb(0x4A4640);
        d.key = rgb(0x2E2B28);
        d.keyHi = rgb(0x4A4540);
        d.keyLo = rgb(0x121110);
        d.keyOff = rgb(0x23211F);
        d.keyOffHi = rgb(0x2E2B28);
        d.keyOffLo = rgb(0x121110);
        d.stepA = rgb(0x2B2926);
        d.stepAHi = rgb(0x403C37);
        d.stepB = rgb(0x47423B);
        d.stepBHi = rgb(0x5E584F);
        d.stepLo = rgb(0x121110);
        d.stepNum = rgb(0xA29B8C);
        d.vfd = rgb(0x120D05);
        d.acid = rgb(0xFFD23F);
        d.onAcid = rgb(0x1A1300);
        d.accOff = rgb(0x3A2C10);
        d.ledOffDisabled = rgb(0x2A2620);
        d.slide = rgb(0x4FC3F7);
        d.slideOff = rgb(0x17303B);
        d.slideTie = rgb(0x4FC3F7).withAlpha(0.75f);
        d.play = rgb(0xFF4A3D);
        d.playOff = rgb(0x3A1E1B);
        d.cursor = rgb(0xFFF4D6);
        d.beak = rgb(0xFF7A1A);
        d.bigCap = d.acid;
        d.bigFace = juce::Colours::white.withAlpha(0.14f);
        d.bigPtr = rgb(0x1A1300);
        d.smallCap = rgb(0x3A3631);
        d.smallStroke = rgb(0x4E4943);
        d.smallPtr = rgb(0xECE6D6);
        d.offCap = rgb(0x242220);
        d.offStroke = rgb(0x2E2B28);
        d.knobShadow = rgb(0x0E0D0C);
        d.knobShadowOff = rgb(0x0E0D0C);
        d.tickOn = rgb(0xECE6D6);
        d.tickOff = rgb(0x4A4640);
        d.tickDisabled = rgb(0x34312C);
        d.value = rgb(0xA29B8C);
        d.valueHover = d.acid;
        d.badgeText = rgb(0x8A8379);
        d.badgeLine = rgb(0x46423C);
        d.patNaCap = rgb(0x211F1D);
        d.patNaHi = rgb(0x2A2825);
        d.patNaText = rgb(0x4A4640);
        d.patSelHi = rgb(0xFFD278);
        d.dashed = rgb(0x6B655C);
        d.switchTrack = rgb(0x0E0D0C);
        d.switchThumb = rgb(0x6B655C);
        d.switchThumbHi = rgb(0x8A8379);
        d.screw = rgb(0x2C2A27);
        d.screwRim = rgb(0x46423C);
        d.screwSlot = rgb(0x121110);
        d.vfdEdge = juce::Colours::white.withAlpha(0.05f);
        d.ledRing = false;
        return d;
    }();
    return t;
}

const Theme& Theme::light()
{
    static const Theme t = [] {
        Theme l{};
        l.kind = ThemeKind::Light;
        l.chassis = rgb(0xD5D2CB);
        l.ink = rgb(0x1C1B19);
        l.label = rgb(0x5A5650);
        l.dim = rgb(0x8F8B83);
        l.line = rgb(0x8F8B83);
        l.ruler = rgb(0x8F8B83);
        l.key = rgb(0xEEECE7);
        l.keyHi = rgb(0xFFFFFF);
        l.keyLo = rgb(0x8F8B83);
        l.keyOff = rgb(0xCFCBC3);
        l.keyOffHi = rgb(0xDAD7D0);
        l.keyOffLo = rgb(0xA9A59D);
        l.stepA = rgb(0xEEECE7);
        l.stepAHi = rgb(0xFFFFFF);
        l.stepB = rgb(0xADA9A0);
        l.stepBHi = rgb(0xC9C5BD);
        l.stepLo = rgb(0x7E7A72);
        l.stepNum = rgb(0x3A3835);
        l.vfd = rgb(0x16110A);
        l.acid = rgb(0xFFB000);
        l.onAcid = rgb(0x1A1300);
        l.accOff = rgb(0x5E4A1A);
        l.ledOffDisabled = rgb(0xA9A59D);
        l.slide = rgb(0x4FC3F7);
        l.slideOff = rgb(0x24414D);
        l.slideTie = rgb(0x1F8FC6);
        l.play = rgb(0xE8352A);
        l.playOff = rgb(0x7A3A34);
        l.cursor = rgb(0xFFF4D6);
        l.beak = rgb(0xFF7A1A);
        l.bigCap = rgb(0x1F1E1C);
        l.bigFace = juce::Colours::white.withAlpha(0.07f);
        l.bigPtr = l.acid;
        l.smallCap = rgb(0x2E2D2A);
        l.smallStroke = rgb(0x444240);
        l.smallPtr = rgb(0xF2EFE8);
        l.offCap = rgb(0xC4C0B8);
        l.offStroke = rgb(0xADA9A0);
        l.knobShadow = juce::Colours::black.withAlpha(0.25f);
        l.knobShadowOff = juce::Colours::black.withAlpha(0.08f);
        l.tickOn = rgb(0x1C1B19);
        l.tickOff = rgb(0xA9A59D);
        l.tickDisabled = rgb(0xC4C0B8);
        l.value = rgb(0x5A5650);
        l.valueHover = l.ink;
        l.badgeText = rgb(0x5A5650);
        l.badgeLine = rgb(0x8F8B83);
        l.patNaCap = rgb(0xC4C0B8);
        l.patNaHi = rgb(0xD5D2CB);
        l.patNaText = rgb(0x8F8B83);
        l.patSelHi = rgb(0xFFD278);
        l.dashed = rgb(0x5A5650);
        l.switchTrack = rgb(0x2A2927);
        l.switchThumb = rgb(0xEEECE7);
        l.switchThumbHi = rgb(0xFFFFFF);
        l.screw = rgb(0xBDB9B1);
        l.screwRim = rgb(0x8F8B83);
        l.screwSlot = rgb(0x5A5650);
        l.vfdEdge = juce::Colours::white.withAlpha(0.5f);
        l.ledRing = true;
        return l;
    }();
    return t;
}

ThemeKind preferredTheme()
{
    if (cachedTheme.load() < 0) {
        juce::PropertiesFile settings(settingsOptions());
        cachedTheme.store(settings.getValue("theme") == "light" ? 1 : 0);
    }
    return static_cast<ThemeKind>(cachedTheme.load());
}

void setPreferredTheme(ThemeKind kind)
{
    cachedTheme.store(static_cast<int>(kind));
    juce::PropertiesFile settings(settingsOptions());
    settings.setValue("theme", kind == ThemeKind::Light ? "light" : "dark");
    settings.saveIfNeeded();
}

Typefaces::Typefaces()
{
    auto load = [](const char* data, int size) {
        return juce::Typeface::createSystemTypefaceFor(data, static_cast<size_t>(size));
    };
    faces[static_cast<size_t>(Face::Ui500)] = load(RdFonts::BarlowCondensedMedium_ttf, RdFonts::BarlowCondensedMedium_ttfSize);
    faces[static_cast<size_t>(Face::Ui600)] = load(RdFonts::BarlowCondensedSemiBold_ttf, RdFonts::BarlowCondensedSemiBold_ttfSize);
    faces[static_cast<size_t>(Face::Ui700)] = load(RdFonts::BarlowCondensedBold_ttf, RdFonts::BarlowCondensedBold_ttfSize);
    faces[static_cast<size_t>(Face::Vfd)] = load(RdFonts::ShareTechMonoRegular_ttf, RdFonts::ShareTechMonoRegular_ttfSize);
    faces[static_cast<size_t>(Face::Name500)] = load(RdFonts::IBMPlexMonoMedium_ttf, RdFonts::IBMPlexMonoMedium_ttfSize);
    faces[static_cast<size_t>(Face::Name700)] = load(RdFonts::IBMPlexMonoBold_ttf, RdFonts::IBMPlexMonoBold_ttfSize);
}

juce::Font font(Face face, float px, float trackingEm)
{
    const juce::SharedResourcePointer<Typefaces> typefaces;
    juce::Font f(juce::FontOptions(typefaces->get(face)).withPointHeight(px));
    // JUCE scales the tracking by the font height (ascent + descent), CSS by the em size.
    if (trackingEm != 0.0f)
        f.setExtraKerningFactor(trackingEm * px / f.getHeight());
    return f;
}

float textWidth(const juce::Font& f, const juce::String& text)
{
    return juce::GlyphArrangement::getStringWidth(f, text);
}

} // namespace rd::ui
