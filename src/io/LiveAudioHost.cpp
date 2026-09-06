#include "io/LiveAudioHost.h"

#include "i18n/Strings.h"

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

    // Output only at startup. Recording is opt-in, and an input opened here
    // would prompt for the microphone before the user asked for anything.
    // setInputEnabled() reopens with inputs when they are actually wanted.
    const auto error = deviceManager.initialiseWithDefaultDevices (0, 2);

    if (error.isNotEmpty())
        return error;

    auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
        return tr (StringId::error_noOutputDevice);

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

juce::String LiveAudioHost::restoreState (const juce::XmlElement& state)
{
    stop();

    // Output only, as start() does, and with the saved state as the preference.
    // A saved setup that named an input is honoured only once setInputEnabled
    // asks for one, so restoring a session never prompts either.
    // initialise falls back to the default device if the saved one has gone,
    // which is the behaviour worth having: a missing interface should not stop
    // the application making sound.
    const auto error = deviceManager.initialise (0, 2, &state, true);

    if (error.isNotEmpty())
        return error;

    if (deviceManager.getCurrentAudioDevice() == nullptr)
        return tr (StringId::error_noOutputDevice);

    deviceManager.addAudioCallback (this);
    started = true;
    return {};
}

juce::String LiveAudioHost::setInputEnabled (bool shouldHaveInput)
{
    if (! started)
        return tr (StringId::error_deviceNotRunning);

    if (inputEnabled == shouldHaveInput)
        return {};

    auto setup = deviceManager.getAudioDeviceSetup();

    if (shouldHaveInput)
    {
        setup.useDefaultInputChannels = true;
    }
    else
    {
        setup.useDefaultInputChannels = false;
        setup.inputChannels.clear();
        setup.inputDeviceName = {};
    }

    // treatAsChosenDevice: this is a deliberate user-visible change, and it
    // should survive into the saved session rather than being forgotten.
    const auto error = deviceManager.setAudioDeviceSetup (setup, true);

    if (error.isNotEmpty())
        return error;

    auto* device = deviceManager.getCurrentAudioDevice();

    // Permission refused, or a device with no inputs: the setup call succeeds
    // and the channels simply are not there. Reporting the request as having
    // worked would leave the user watching a meter that can never move.
    if (shouldHaveInput && (device == nullptr || device->getActiveInputChannels().isZero()))
    {
        inputEnabled = false;
        return tr (StringId::error_noInputDevice);
    }

    inputEnabled = shouldHaveInput;
    return {};
}

void LiveAudioHost::stop()
{
    if (! started)
        return;

    recorder.stop();
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
    started = false;
    inputEnabled = false;
}

juce::String LiveAudioHost::describeDevice() const
{
    if (auto* device = deviceManager.getCurrentAudioDevice())
        return tr (StringId::status_audioDevice,
                   Args {}
                       .with ("device", device->getName())
                       .with ("rate", juce::String (device->getCurrentSampleRate(), 0))
                       .with ("samples", device->getCurrentBufferSizeSamples()));

    return tr (StringId::status_noAudioDevice);
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

void LiveAudioHost::audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                                      int numInputChannels,
                                                      float* const* outputChannelData,
                                                      int numOutputChannels, int numSamples,
                                                      const juce::AudioIODeviceCallbackContext&)
{
    // Before the render, so a take captures the input that arrived with this
    // block rather than the one after it. The recorder also keeps the input
    // meter running when nothing is being recorded.
    recorder.writeBlock (inputChannelData, numInputChannels, numSamples);

    // The engine always renders stereo. A device with a different channel count
    // gets the stereo pair spread across it rather than silence.
    if (scratch.getNumSamples() < numSamples)
    {
        // Silence, not whatever the driver left in the buffer. Returning without
        // writing hands the device its own stale memory, so a block the engine
        // could not fill came out as a click rather than as a gap.
        for (int channel = 0; channel < numOutputChannels; ++channel)
            if (outputChannelData[channel] != nullptr)
                juce::FloatVectorOperations::clear (outputChannelData[channel], numSamples);

        return; // cannot allocate here; the next prepare() will size it
    }

    juce::AudioBuffer<float> view (scratch.getArrayOfWritePointers(), 2, 0, numSamples);
    engine.processBlock (view);

    for (int channel = 0; channel < numOutputChannels; ++channel)
    {
        if (outputChannelData[channel] == nullptr)
            continue;

        const auto source = juce::jmin (channel, 1);
        juce::FloatVectorOperations::copy (outputChannelData[channel], view.getReadPointer (source),
                                           numSamples);
    }
}

} // namespace dew
