#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include "AudioEngine.h"
#include "AudioRecorder.h"

namespace dew
{

/** Connects the engine to the machine's sound hardware.

    The only part of the audio path that knows a device exists. Everything about
    how a project turns into samples lives in AudioEngine, which is why the
    offline renderer can exercise the same code without one.

    The device opens OUTPUT-ONLY, and inputs are added later by
    setInputEnabled(). That is deliberate and worth keeping: opening an input at
    startup makes macOS ask for the microphone before the user has asked dew for
    anything, and a permission prompt with no context attached is one people
    refuse. Arming a channel or choosing an input in Audio Settings is the
    moment the request explains itself.
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

    /** Reopens the device with, or without, input channels.

        Returns an empty string on success, or a message for the user - which on
        macOS includes the case where the microphone permission was refused, and
        the device comes back with no input channels at all.
    */
    juce::String setInputEnabled (bool shouldHaveInput);

    bool isInputEnabled() const noexcept  { return inputEnabled; }

    juce::AudioDeviceManager& getDeviceManager() noexcept  { return deviceManager; }

    /** Where captured input goes. The host owns it because the device callback
        is the only place input samples exist.
    */
    AudioRecorder& getRecorder() noexcept              { return recorder; }
    const AudioRecorder& getRecorder() const noexcept  { return recorder; }

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
    AudioRecorder recorder;
    juce::AudioDeviceManager deviceManager;
    juce::AudioBuffer<float> scratch;
    bool started = false;
    bool inputEnabled = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LiveAudioHost)
};

} // namespace dew
