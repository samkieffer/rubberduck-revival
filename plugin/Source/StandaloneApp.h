#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

// What the standalone app does on top of JUCE's standalone wrapper. Nothing
// here acts in a host.
namespace standalone {

// The app's audio devices, or nullptr in a host (and in the tests).
juce::AudioDeviceManager* deviceManager();

// Starts the app as a new machine, like a new instance in a host, instead of
// the one JUCE kept from the last quit: forgets that state. To be called while
// the app creates the processor, before JUCE restores it.
void forgetLastState();

// Keeps the app on the Mac's sound output (Sound settings, the jack plugged in
// or out). JUCE stays on the device it opened at launch and, once that one is
// gone, opens none: unplugging the jack left the app silent. A device chosen in
// the app's Options stays until the Mac's output changes.
class OutputFollower : private juce::Timer
{
public:
    explicit OutputFollower(juce::AudioDeviceManager& manager);

private:
    void timerCallback() override;

    juce::AudioDeviceManager& manager;
    juce::String followed;  // the Mac's output the app was last put on
};

} // namespace standalone
