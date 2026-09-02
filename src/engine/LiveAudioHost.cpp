#include "LiveAudioHost.h"

namespace dew
{

LiveAudioHost::LiveAudioHost (AudioEngine& e)
    : engine (e)
{
}

LiveAudioHost::~LiveAudioHost()
{
    stop();
}

juce::String LiveAudioHost::start()
{
    if (started)
        return {};

    // Output only: dew never records, so asking for an input would trigger a
    // microphone permission prompt for no reason.
    const auto error = deviceManager.initialiseWithDefaultDevices (0, 2);

    if (error.isNotEmpty())
        return error;

    auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
        return "No audio output device is available.";

    // The default buffer can be as small as 16 samples, which is 0.4 ms and
    // will glitch under any load. Ask for something a software synth can
    // actually meet, falling back to whatever the device offers nearest.
    if (device->getCurrentBufferSizeSamples() < preferredBufferSize)
    {
        auto setup = deviceManager.getAudioDeviceSetup();
        setup.bufferSize = preferredBufferSize;

        // Non-fatal: a device that refuses simply keeps the size it had.
        deviceManager.setAudioDeviceSetup (setup, true);
    }

    deviceManager.addAudioCallback (this);
    started = true;
    return {};
}

void LiveAudioHost::stop()
{
    if (! started)
        return;

    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
    started = false;
}

juce::String LiveAudioHost::describeDevice() const
{
    if (auto* device = deviceManager.getCurrentAudioDevice())
        return device->getName() + "  ·  " + juce::String (device->getCurrentSampleRate(), 0) + " Hz"
             + "  ·  " + juce::String (device->getCurrentBufferSizeSamples()) + " samples";

    return "no audio device";
}

void LiveAudioHost::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const auto sampleRate = device->getCurrentSampleRate();
    const auto blockSize = device->getCurrentBufferSizeSamples();

    scratch.setSize (2, juce::jmax (1, blockSize));
    engine.prepare (sampleRate, blockSize);
}

void LiveAudioHost::audioDeviceStopped()
{
    engine.releaseResources();
}

void LiveAudioHost::audioDeviceIOCallbackWithContext (const float* const*, int,
                                                      float* const* outputChannelData,
                                                      int numOutputChannels,
                                                      int numSamples,
                                                      const juce::AudioIODeviceCallbackContext&)
{
    // The engine always renders stereo. A device with a different channel count
    // gets the stereo pair spread across it rather than silence.
    if (scratch.getNumSamples() < numSamples)
        return;   // cannot allocate here; the next prepare() will size it

    juce::AudioBuffer<float> view (scratch.getArrayOfWritePointers(), 2, 0, numSamples);
    engine.processBlock (view);

    for (int channel = 0; channel < numOutputChannels; ++channel)
    {
        if (outputChannelData[channel] == nullptr)
            continue;

        const auto source = juce::jmin (channel, 1);
        juce::FloatVectorOperations::copy (outputChannelData[channel],
                                           view.getReadPointer (source),
                                           numSamples);
    }
}

} // namespace dew
