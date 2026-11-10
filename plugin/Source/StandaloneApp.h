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

// Decides when the app must ask again for the output's rate. JUCE keeps the
// rate it asked for when CoreAudio accepts the request without applying it: a
// Bluetooth headset in call mode stays at 16 kHz while JUCE goes on at 44.1 kHz,
// and everything plays 2.76 times slower and lower.
class RateCheck
{
public:
    // To be called at each look, with the rate JUCE believes the device runs at
    // and the one the device reports (0 = unknown). Returns the rate to ask
    // for, or 0: a difference has to be seen twice in a row (JUCE follows most
    // changes by itself, a moment later) and is asked for once only.
    double look(const juce::String& device, double believed, double real);

private:
    juce::String seenOn, askedOn;
    double seen = 0.0, asked = 0.0;
};

// Keeps the app on the Mac's sound output (Sound settings, the jack plugged in
// or out). JUCE stays on the device it opened at launch and, once that one is
// gone, opens none: unplugging the jack left the app silent. A device chosen in
// the app's Options stays until the Mac's output changes. Keeps the app at the
// output's real rate too (see RateCheck).
class OutputFollower : private juce::Timer
{
public:
    explicit OutputFollower(juce::AudioDeviceManager& manager);

private:
    void timerCallback() override;
    void followOutput();
    void matchRate();

    juce::AudioDeviceManager& manager;
    juce::String followed;  // the Mac's output the app was last put on
    RateCheck rateCheck;
};

} // namespace standalone
