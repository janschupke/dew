#include "engine/SynthChannel.h"

namespace dew
{

void SynthChannel::prepare (double sampleRate)
{
    // Touching the bank here is deliberate. Generating it allocates and runs a
    // few hundred FFTs, and it is built on first use - so the FIRST caller must
    // never be the audio thread. prepare() is always the message thread.
    (void) wavetableAt (0);

    for (auto& voice : voices)
        voice.prepare (sampleRate);
}

void SynthChannel::reset() noexcept
{
    for (auto& voice : voices)
        voice.reset();
}

void SynthChannel::noteOn (int pitch, float velocity, const OscBankSnapshot& osc,
                           const AmpSettings& amp, int durationSamples, int startOffset)
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
        target->start (pitch, velocity, osc, amp, durationSamples, startOffset);
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

void SynthChannel::renderAdd (float* buffer, int numSamples, float bendSemitones, float modulation,
                              const OscBankSnapshot* live) noexcept
{
    for (auto& voice : voices)
    {
        voice.setPitchModulation (bendSemitones, modulation, numSamples);

        if (live != nullptr)
            voice.setWavetablePosition (*live);

        voice.renderAdd (buffer, numSamples);
    }
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
