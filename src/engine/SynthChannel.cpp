#include "SynthChannel.h"

namespace dew
{

void SynthChannel::prepare (double sampleRate)
{
    for (auto& voice : voices)
        voice.prepare (sampleRate);
}

void SynthChannel::reset() noexcept
{
    for (auto& voice : voices)
        voice.reset();
}

void SynthChannel::noteOn (int pitch, float velocity, const OscSettings& osc,
                           const AmpSettings& amp, int durationSamples)
{
    SynthVoice* target = nullptr;

    for (auto& voice : voices)
    {
        if (! voice.isActive())
        {
            target = &voice;
            break;
        }
    }

    if (target == nullptr)
    {
        // All busy: steal the one that has been sounding longest.
        juce::int64 oldest = -1;

        for (auto& voice : voices)
        {
            if (voice.getAge() > oldest)
            {
                oldest = voice.getAge();
                target = &voice;
            }
        }
    }

    if (target != nullptr)
        target->start (pitch, velocity, osc, amp, durationSamples);
}

void SynthChannel::noteOff (int pitch) noexcept
{
    for (auto& voice : voices)
        if (voice.getPitch() == pitch)
            voice.release();
}

void SynthChannel::allNotesOff() noexcept
{
    for (auto& voice : voices)
        if (voice.isActive())
            voice.release();
}

void SynthChannel::renderAdd (float* buffer, int numSamples) noexcept
{
    for (auto& voice : voices)
        voice.renderAdd (buffer, numSamples);
}

int SynthChannel::countActiveVoices() const noexcept
{
    int count = 0;

    for (const auto& voice : voices)
        if (voice.isActive())
            ++count;

    return count;
}

} // namespace dew
