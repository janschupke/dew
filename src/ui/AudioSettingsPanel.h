#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "io/LiveAudioHost.h"
#include "ui/primitives/DewControls.h"

namespace dew
{

/** Choosing the audio device, in dew's own idiom.

    juce::AudioDeviceSelectorComponent would give all of this for free, and it
    looks like a JUCE demo dropped into the application. This is the same
    AudioDeviceManager underneath - the one LiveAudioHost::getDeviceManager has
    exposed all along with no callers.

    The input defaults to None, and opening one is what asks macOS for the
    microphone. That request is deliberately deferred to here (or to arming a
    channel) rather than made at startup: a permission prompt that arrives
    before the user has asked dew for anything is one people refuse.

    Choosing an input therefore does two things - it names the device, and it
    tells LiveAudioHost to reopen with input channels. The meter beside it is
    the confirmation that both worked, which a combo box alone cannot give.
*/
class AudioSettingsPanel : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    AudioSettingsPanel (LiveAudioHost&, AudioEngine&);
    ~AudioSettingsPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Opens it in a dialog window owned by JUCE. */
    static void show (LiveAudioHost&, AudioEngine&, juce::Component* parent);

    /** Called when the device changes, so the caller can persist or report it. */
    std::function<void()> onDeviceChanged;

    // --- for tests -----------------------------------------------------------
    juce::String getSummaryText() const
    {
        return summaryText;
    }
    int getNumDeviceOptions() const
    {
        return outputBox.getNumItems();
    }

    /** Whether an input is selected. The meter is only meaningful when it is. */
    bool isInputSelected() const
    {
        return inputBox.getSelectedId() > 1;
    }

    /** The last input level drawn, 0..1. */
    float getInputLevel() const noexcept
    {
        return inputLevel;
    }

    static constexpr int preferredWidth = 420;

    // Taller than it was by one row and the meter: the input is no longer a
    // combo box nobody reads.
    static constexpr int preferredHeight = 392;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    /** Where the input meter is drawn. */
    /** Set by resized(), read by paint() and by the meter's own repaint - a
        painter that computes its own layout is a second layout. */
    juce::Rectangle<int> meterArea;

    void rebuildLists();
    void updateSummary();
    void applySetup (const juce::AudioDeviceManager::AudioDeviceSetup&);

    LiveAudioHost& audioHost;
    juce::AudioDeviceManager& deviceManager;
    AudioEngine& engine;

    DewDropdown typeBox, outputBox, inputBox, inputChannelBox, rateBox, bufferBox;

    /** Falls back rather than snapping, so a meter reads as a level rather than
        as a flicker. Decay per frame at the refresh rate the tokens declare.
    */
    float inputLevel = 0.0f;
    juce::Array<juce::Rectangle<int>> labelBounds;
    juce::StringArray labels;

    DewButton testButton { "Test tone", DewButton::Role::normal };

    juce::String summaryText;
    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioSettingsPanel)
};

} // namespace dew
