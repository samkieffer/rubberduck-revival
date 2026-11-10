#include "StandaloneApp.h"

#include <juce_audio_utils/juce_audio_utils.h>

#if JucePlugin_Build_Standalone
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

#if defined(__APPLE__)
#include <CoreAudio/CoreAudio.h>
#endif

#include <cmath>
#include <vector>

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

// The rate the output named as JUCE lists it really runs at; 0 when unknown.
double outputRate(const juce::String& name)
{
#if defined(__APPLE__)
    AudioObjectPropertyAddress address{kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal,
                                       kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0, nullptr, &size) != noErr)
        return 0.0;
    std::vector<AudioObjectID> devices(size / sizeof(AudioObjectID));
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size, devices.data()) != noErr)
        return 0.0;
    devices.resize(size / sizeof(AudioObjectID));
    for (const auto device : devices) {
        address = {kAudioDevicePropertyStreams, kAudioObjectPropertyScopeOutput, kAudioObjectPropertyElementMain};
        if (AudioObjectGetPropertyDataSize(device, &address, 0, nullptr, &size) != noErr || size == 0)
            continue;  // no output
        address = {kAudioDevicePropertyDeviceNameCFString, kAudioObjectPropertyScopeGlobal,
                   kAudioObjectPropertyElementMain};
        CFStringRef deviceName = nullptr;
        size = sizeof deviceName;
        if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &deviceName) != noErr
            || deviceName == nullptr)
            continue;
        const bool found = juce::String::fromCFString(deviceName) == name;
        CFRelease(deviceName);
        if (!found)
            continue;
        address.mSelector = kAudioDevicePropertyNominalSampleRate;
        Float64 rate = 0.0;
        size = sizeof rate;
        if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &rate) != noErr)
            return 0.0;
        return rate;
    }
#else
    juce::ignoreUnused(name);
#endif
    return 0.0;
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

double RateCheck::look(const juce::String& device, double believed, double real)
{
    if (real <= 0.0 || believed <= 0.0 || std::abs(real - believed) < 0.5) {
        seenOn.clear();
        askedOn.clear();
        return 0.0;
    }
    const bool twice = device == seenOn && std::abs(real - seen) < 0.5;
    seenOn = device;
    seen = real;
    if (!twice || (device == askedOn && std::abs(real - asked) < 0.5))
        return 0.0;
    askedOn = device;
    asked = real;
    return real;
}

void OutputFollower::timerCallback()
{
    followOutput();
    matchRate();
}

void OutputFollower::followOutput()
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

void OutputFollower::matchRate()
{
    auto* device = manager.getCurrentAudioDevice();
    if (device == nullptr)
        return;
    auto setup = manager.getAudioDeviceSetup();
    const double rate = rateCheck.look(setup.outputDeviceName, device->getCurrentSampleRate(),
                                       outputRate(setup.outputDeviceName));
    if (rate <= 0.0)
        return;
    setup.sampleRate = rate;
    manager.setAudioDeviceSetup(setup, true);
}

} // namespace standalone
