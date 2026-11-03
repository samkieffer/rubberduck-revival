#include "StandaloneApp.h"

#include <juce_audio_utils/juce_audio_utils.h>

#if JucePlugin_Build_Standalone
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

#if defined(__APPLE__)
#include <CoreAudio/CoreAudio.h>
#endif

namespace standalone {

namespace {

#if JucePlugin_Build_Standalone
juce::StandalonePluginHolder* holder()
{
    return juce::StandalonePluginHolder::getInstance();
}
#endif

// The Mac's sound output, under the name JUCE lists it with; empty elsewhere.
juce::String systemOutputName()
{
#if defined(__APPLE__)
    AudioObjectPropertyAddress address{kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal,
                                       kAudioObjectPropertyElementMain};
    AudioObjectID device = kAudioObjectUnknown;
    UInt32 size = sizeof device;
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size, &device) != noErr
        || device == kAudioObjectUnknown)
        return {};
    address.mSelector = kAudioDevicePropertyDeviceNameCFString;  // what JUCE names devices by
    CFStringRef name = nullptr;
    size = sizeof name;
    if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &name) != noErr || name == nullptr)
        return {};
    const auto result = juce::String::fromCFString(name);
    CFRelease(name);
    return result;
#else
    return {};
#endif
}

} // namespace

juce::AudioDeviceManager* deviceManager()
{
#if JucePlugin_Build_Standalone
    if (auto* h = holder())
        return &h->deviceManager;
#endif
    return nullptr;
}

void forgetLastState()
{
#if JucePlugin_Build_Standalone
    // JUCE's own "Reset to default state" does the same before creating the
    // processor again.
    if (auto* h = holder())
        if (auto* settings = h->settings.get())
            settings->removeValue("filterState");
#endif
}

OutputFollower::OutputFollower(juce::AudioDeviceManager& m) : manager(m)
{
    startTimer(500);
}

void OutputFollower::timerCallback()
{
    const auto name = systemOutputName();
    if (name.isEmpty() || name == followed)
        return;
    auto* type = manager.getCurrentDeviceTypeObject();
    // A device just plugged in is listed once JUCE has scanned again: next time.
    if (type == nullptr || !type->getDeviceNames(false).contains(name))
        return;
    auto setup = manager.getAudioDeviceSetup();
    if (setup.outputDeviceName != name || manager.getCurrentAudioDevice() == nullptr) {
        setup.outputDeviceName = name;
        if (manager.setAudioDeviceSetup(setup, true).isNotEmpty())
            return;  // try again next time
    }
    followed = name;
}

} // namespace standalone
