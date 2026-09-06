#include "engine/SoundFontChannel.h"

namespace dew
{

void SoundFontChannel::prepare (double sampleRate) noexcept
{
    for (auto& voice : voices)
        voice.prepare (sampleRate);
}

void SoundFontChannel::reset() noexcept
{
    for (auto& voice : voices)
        voice.reset();
}

SoundFontVoice& SoundFontChannel::voiceToUse() noexcept
{
    SoundFontVoice* oldest = &voices[0];

    for (auto& voice : voices)
    {
        if (! voice.isActive())
            return voice;

        if (voice.getAge() > oldest->getAge())
            oldest = &voice;
    }

    return *oldest;
}

void SoundFontChannel::noteOn (const SoundFontData& font, const SoundFontPreset& preset, int pitch,
                               float velocity, const SoundFontSettings& settings,
                               int durationSamples, int startOffset) noexcept
{
    const auto key = juce::jlimit (0, 127, pitch);
    const auto vel = juce::jlimit (1, 127, (int) (velocity * 127.0f + 0.5f));

    for (const auto& region : preset.regions)
    {
        if (! region.matches (key, vel))
            continue;

        // Before the voice is taken, not after: a region cutting its own class
        // would silence the note it is starting.
        if (region.exclusiveClass != 0)
            for (auto& voice : voices)
                if (voice.isActive() && voice.getExclusiveClass() == region.exclusiveClass)
                    voice.cut();

        voiceToUse().start (font, region, key, velocity, settings, durationSamples, startOffset);
    }
}

void SoundFontChannel::noteOff (int pitch) noexcept
{
    for (auto& voice : voices)
        if (voice.isActive() && voice.getPitch() == pitch)
            voice.release();
}

void SoundFontChannel::allNotesOff() noexcept
{
    for (auto& voice : voices)
        if (voice.isActive())
            voice.release();
}

void SoundFontChannel::renderAdd (float* left, float* right, int numSamples) noexcept
{
    for (auto& voice : voices)
        if (voice.isActive())
            voice.renderAdd (left, right, numSamples);
}

int SoundFontChannel::countActiveVoices() const noexcept
{
    int count = 0;

    for (const auto& voice : voices)
        if (voice.isActive())
            ++count;

    return count;
}

NoteMask SoundFontChannel::soundingPitches() const noexcept
{
    NoteMask mask;

    for (const auto& voice : voices)
        if (voice.isActive())
            mask.set (voice.getPitch());

    return mask;
}

} // namespace dew
