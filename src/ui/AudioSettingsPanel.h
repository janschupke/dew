#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../engine/AudioEngine.h"
#include "../engine/LiveAudioHost.h"
#include "primitives/DewControls.h"

namespace dew
{

/** Choosing the audio device, in dew's own idiom.

    juce::AudioDeviceSelectorComponent would give all of this for free, and it
    looks like a JUCE demo dropped into the application. This is the same
    AudioDeviceManager underneath - the one LiveAudioHost::getDeviceManager has
    exposed all along with no callers.

    dew never records, so the input defaults to None: asking for an input at
    startup triggers a microphone permission prompt for nothing.
*/
class AudioSettingsPanel : public juce::Component,
                           private juce::ChangeListener
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
    juce::String getSummaryText() const { return summaryText; }
    int getNumDeviceOptions() const     { return outputBox.getNumItems(); }

    static constexpr int preferredWidth = 420;
    static constexpr int preferredHeight = 320;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void rebuildLists();
    void updateSummary();
    void applySetup (const juce::AudioDeviceManager::AudioDeviceSetup&);

    juce::AudioDeviceManager& deviceManager;
    AudioEngine& engine;

    juce::ComboBox typeBox, outputBox, inputBox, rateBox, bufferBox;
    juce::Array<juce::Rectangle<int>> labelBounds;
    juce::StringArray labels;

    DewButton testButton { "Test tone", DewButton::Role::normal };

    juce::String summaryText;
    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioSettingsPanel)
};

} // namespace dew
