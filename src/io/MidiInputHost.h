#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include "io/MidiRouter.h"

namespace dew
{

/** Connects the engine to the machine's MIDI controllers.

    The MIDI counterpart of LiveAudioHost, and the only part of the input path
    that knows a port exists. It deliberately does NOT own juce::MidiInput
    objects: AudioDeviceManager already opens them, persists which were enabled
    in the same state XML dew already round-trips, and broadcasts a change when
    the device list moves. Re-implementing that would be a second source of
    truth for no gain.

    What it does own is the answer to "which devices does the USER want", which
    AudioDeviceManager gets subtly wrong. Its own wanted-list is populated only
    when initialise() is handed a state XML, so a device ticked during this
    session is not in it. Unplug that device and plug it back in and the manager
    still reports it enabled - a dead port is still in its vector - while no
    messages arrive, and it will not reopen a device it believes is already
    open. The controller stays silent until the user unticks and reticks it.

    So `wanted` is kept here, and reconcile() runs on every device-list change:
    a wanted device that has vanished is force-disabled so the stale port is
    dropped, and one that has come back is enabled again.
*/
class MidiInputHost : private juce::ChangeListener
{
public:
    MidiInputHost (juce::AudioDeviceManager&, AudioEngine&);
    ~MidiInputHost() override;

    /** Starts listening. Adopts anything the manager already had enabled - on
        launch that is whatever the saved state restored.
    */
    void start();
    void stop();

    // --- devices -------------------------------------------------------------
    /** What the machine currently offers. */
    static juce::Array<juce::MidiDeviceInfo> getAvailableDevices();

    /** Every device the panel should show: what is present, plus anything the
        user asked for that is not. A device the user ticked does not vanish
        from the list when its cable does - its tick has to stay visible, and
        explain itself.
    */
    juce::Array<juce::MidiDeviceInfo> getListedDevices() const;

    void setDeviceEnabled (const juce::String& identifier, bool enabled);

    /** True if the user has asked for this device, whether or not it is here. */
    bool isDeviceWanted (const juce::String& identifier) const;

    /** True if it is actually open and delivering. */
    bool isDeviceConnected (const juce::String& identifier) const;

    int getNumWantedDevices() const
    {
        return wanted.size();
    }

    /** True if at least one wanted device is actually open and delivering.

        Deliberately distinct from getNumWantedDevices: a device the user has
        ticked whose cable is out is wanted but not listening, and reporting
        those as the same thing is how a panel ends up showing a healthy green
        light beside the words "not connected".
    */
    bool hasConnectedInput() const;

    /** Describes what is listening, for the status line. */
    juce::String describeInputs() const;

    MidiRouter& getRouter() noexcept
    {
        return router;
    }
    const MidiRouter& getRouter() const noexcept
    {
        return router;
    }

    /** Called after the device list is reconciled, so a panel can rebuild. */
    std::function<void()> onDevicesChanged;

    /** What reconciling should do about one wanted device.

        Pulled out as a pure function on purpose. The rule it encodes is the
        whole reason this class exists, and leaving it inline would make it
        provable only on a machine with a controller plugged in - which CI is
        not. Here it can be checked exhaustively with no hardware at all.
    */
    enum class Action
    {
        none,
        open,
        close
    };

    static Action actionFor (bool present, bool enabled) noexcept
    {
        if (present && ! enabled)
            return Action::open; // back again, or newly ticked

        if (! present && enabled)
            return Action::close; // gone: drop the dead port, keep the choice

        return Action::none;
    }

    // --- for tests -----------------------------------------------------------
    /** Reconciles against an explicit device list instead of the machine's, so
        unplugging and replugging can be driven in a test with no hardware.
    */
    void reconcileWith (const juce::Array<juce::MidiDeviceInfo>& present);

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    /** The MIDI thread. A plain forwarder to the router, which holds every
        rule; this class only decides which ports exist.
    */
    struct Callback : public juce::MidiInputCallback
    {
        explicit Callback (MidiRouter& r)
            : router (r)
        {
        }

        void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message) override
        {
            router.handleMessage (message);
        }

        MidiRouter& router;
    };

    juce::AudioDeviceManager& deviceManager;
    MidiRouter router;
    Callback callback { router };

    /** Identifiers the user has asked for, present or not. The source of truth
        reconcile() works from.
    */
    juce::StringArray wanted;

    bool started = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiInputHost)
};

} // namespace dew
