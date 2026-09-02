#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include "AudioEngine.h"

namespace dew
{

/** Connects the engine to the machine's sound output.

    The only part of the audio path that knows a device exists. Everything about
    how a project turns into samples lives in AudioEngine, which is why the
    offline renderer can exercise the same code without one.
*/
class LiveAudioHost : private juce::AudioIODeviceCallback
{
public:
    explicit LiveAudioHost (AudioEngine&);
    ~LiveAudioHost() override;

    /** Opens the default output device. Returns an empty string on success, or
        a message suitable for showing to the user.
    */
    juce::String start();

    /** Opens the device described by a previously saved state instead of the
        default one. Falls back to the default if that device has gone.
    */
    juce::String restoreState (const juce::XmlElement&);

    void stop();

    juce::AudioDeviceManager& getDeviceManager() noexcept  { return deviceManager; }

    /** Describes the running device, for the status line. */
    juce::String describeDevice() const;

    /** Buffer size asked for at startup. Small enough to feel responsive, large
        enough that a software synth can fill it reliably.
    */
    static constexpr int preferredBufferSize = 256;

private:
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;

    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;

    AudioEngine& engine;
    juce::AudioDeviceManager deviceManager;
    juce::AudioBuffer<float> scratch;
    bool started = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LiveAudioHost)
};

} // namespace dew
