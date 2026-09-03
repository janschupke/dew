#include "io/MidiInputHost.h"

namespace dew
{

MidiInputHost::MidiInputHost (juce::AudioDeviceManager& manager, AudioEngine& engine)
    : deviceManager (manager)
    , router (engine)
{
}

MidiInputHost::~MidiInputHost()
{
    stop();
}

void MidiInputHost::start()
{
    if (started)
        return;

    // An empty identifier means every enabled input, so one registration
    // covers devices enabled later without re-registering per device.
    deviceManager.addMidiInputDeviceCallback ({}, &callback);
    deviceManager.addChangeListener (this);
    started = true;

    // Adopt whatever is already enabled. On launch that is what the saved
    // state restored, which is how a remembered controller starts working
    // without the user opening the panel at all.
    for (const auto& device : getAvailableDevices())
        if (deviceManager.isMidiInputDeviceEnabled (device.identifier))
            wanted.addIfNotAlreadyThere (device.identifier);
}

void MidiInputHost::stop()
{
    if (! started)
        return;

    deviceManager.removeChangeListener (this);
    deviceManager.removeMidiInputDeviceCallback ({}, &callback);
    router.reset();
    started = false;
}

juce::Array<juce::MidiDeviceInfo> MidiInputHost::getAvailableDevices()
{
    return juce::MidiInput::getAvailableDevices();
}

juce::Array<juce::MidiDeviceInfo> MidiInputHost::getListedDevices() const
{
    auto devices = getAvailableDevices();

    // Then anything wanted that is not here, so a ticked device keeps its row
    // when its cable is pulled instead of silently disappearing.
    for (const auto& identifier : wanted)
    {
        const auto present = std::any_of (devices.begin(), devices.end(), [&] (const auto& d)
                                          { return d.identifier == identifier; });

        if (! present)
            devices.add ({ {}, identifier });
    }

    return devices;
}

bool MidiInputHost::isDeviceWanted (const juce::String& identifier) const
{
    return wanted.contains (identifier);
}

bool MidiInputHost::isDeviceConnected (const juce::String& identifier) const
{
    const auto devices = getAvailableDevices();

    return std::any_of (devices.begin(), devices.end(),
                        [&] (const auto& d) { return d.identifier == identifier; })
           && deviceManager.isMidiInputDeviceEnabled (identifier);
}

bool MidiInputHost::hasConnectedInput() const
{
    for (const auto& device : getAvailableDevices())
        if (deviceManager.isMidiInputDeviceEnabled (device.identifier))
            return true;

    return false;
}

void MidiInputHost::setDeviceEnabled (const juce::String& identifier, bool enabled)
{
    if (identifier.isEmpty())
        return;

    if (enabled)
        wanted.addIfNotAlreadyThere (identifier);
    else
        wanted.removeString (identifier);

    deviceManager.setMidiInputDeviceEnabled (identifier, enabled);

    if (! enabled)
    {
        // A controller unticked mid-note would otherwise leave that note
        // sounding with nothing left to release it.
        router.reset();
    }

    if (onDevicesChanged != nullptr)
        onDevicesChanged();
}

void MidiInputHost::reconcileWith (const juce::Array<juce::MidiDeviceInfo>& present)
{
    for (const auto& identifier : wanted)
    {
        const auto here = std::any_of (present.begin(), present.end(),
                                       [&] (const auto& d) { return d.identifier == identifier; });

        switch (actionFor (here, deviceManager.isMidiInputDeviceEnabled (identifier)))
        {
            case Action::open:
                // Back again. This is the replug the manager will not handle on
                // its own for a device enabled during this session.
                deviceManager.setMidiInputDeviceEnabled (identifier, true);
                break;

            case Action::close:
                // Gone, but the manager still holds a dead port and would
                // refuse to reopen it later because it believes it is already
                // open. Drop it, and keep it in `wanted` so it comes back when
                // the cable does.
                deviceManager.setMidiInputDeviceEnabled (identifier, false);
                router.reset();
                break;

            case Action::none: break;
        }
    }

    if (onDevicesChanged != nullptr)
        onDevicesChanged();
}

void MidiInputHost::changeListenerCallback (juce::ChangeBroadcaster*)
{
    reconcileWith (getAvailableDevices());
}

juce::String MidiInputHost::describeInputs() const
{
    juce::StringArray names;

    for (const auto& device : getAvailableDevices())
        if (deviceManager.isMidiInputDeviceEnabled (device.identifier))
            names.add (device.name);

    if (names.isEmpty())
        return wanted.isEmpty() ? "no MIDI input" : "MIDI input not connected";

    return names.joinIntoString (", ");
}

} // namespace dew
