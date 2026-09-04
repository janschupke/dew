#include "engine/SoundFontVoice.h"

#include <cmath>

namespace dew
{

namespace
{

/** Below this the tail is inaudible and the voice is worth more than the
    silence it is producing. -100dB, which is the fall the format's decay and
    release times are defined against. */
constexpr float kSilenceThreshold = 1.0e-5f;

int secondsToSamples (float seconds, double sampleRate) noexcept
{
    return (int) juce::jlimit (0.0, 1.0e7, (double) seconds * sampleRate);
}

/** The per-sample factor that falls a hundred decibels in `samples`.

    A hundred decibels over the stated time is what the format means by a decay
    or release time, so the rate is fixed and the stage ends when it reaches the
    sustain level - not when the time runs out. Zero samples means "at once".
*/
float fallFactorFor (int samples) noexcept
{
    if (samples <= 0)
        return 0.0f;

    return (float) std::pow (10.0, -100.0 / (20.0 * (double) samples));
}

} // namespace

// ==============================================================================

void SoundFontVoice::Envelope::start (const SoundFontEnvelope& e, double sampleRate,
                                      float attackScale, float releaseScale) noexcept
{
    const auto delaySamples = secondsToSamples (e.delaySeconds, sampleRate);
    attackSamples = secondsToSamples (e.attackSeconds * attackScale, sampleRate);
    holdSamples = secondsToSamples (e.holdSeconds, sampleRate);

    sustainLevel = juce::jlimit (0.0f, 1.0f, e.sustainLevel);
    decayFactor = fallFactorFor (secondsToSamples (e.decaySeconds, sampleRate));
    releaseFactor = fallFactorFor (secondsToSamples (e.releaseSeconds * releaseScale, sampleRate));

    attackIncrement = attackSamples > 0 ? 1.0f / (float) attackSamples : 1.0f;

    if (delaySamples > 0)
    {
        stage = Stage::delay;
        samplesLeft = delaySamples;
        level = 0.0f;
    }
    else if (attackSamples > 0)
    {
        stage = Stage::attack;
        samplesLeft = attackSamples;
        level = 0.0f;
    }
    else
    {
        stage = Stage::hold;
        samplesLeft = holdSamples;
        level = 1.0f;
    }
}

void SoundFontVoice::Envelope::release() noexcept
{
    if (stage == Stage::idle || stage == Stage::release)
        return;

    stage = Stage::release;
}

float SoundFontVoice::Envelope::nextSample() noexcept
{
    switch (stage)
    {
        case Stage::idle: return 0.0f;

        case Stage::delay:
            if (--samplesLeft <= 0)
            {
                stage = attackSamples > 0 ? Stage::attack : Stage::hold;
                samplesLeft = attackSamples > 0 ? attackSamples : holdSamples;
                level = attackSamples > 0 ? 0.0f : 1.0f;
            }

            return 0.0f;

        case Stage::attack:
            level += attackIncrement;

            if (--samplesLeft <= 0 || level >= 1.0f)
            {
                level = 1.0f;
                stage = Stage::hold;
                samplesLeft = holdSamples;
            }

            return level;

        case Stage::hold:
            if (--samplesLeft <= 0)
                stage = Stage::decay;

            return level;

        case Stage::decay:
            level *= decayFactor;

            if (level <= sustainLevel)
            {
                level = sustainLevel;
                stage = Stage::sustain;

                // A sustain of nothing is the end of the note, not a stage to
                // sit in: a percussive region decays to silence and would
                // otherwise hold a voice open for as long as the key is down.
                if (level <= kSilenceThreshold)
                    stage = Stage::idle;
            }

            return level;

        case Stage::sustain: return level;

        case Stage::release:
            level *= releaseFactor;

            if (level <= kSilenceThreshold)
            {
                level = 0.0f;
                stage = Stage::idle;
            }

            return level;
    }

    return 0.0f;
}

// ==============================================================================

void SoundFontVoice::prepare (double sampleRate) noexcept
{
    engineSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    reset();
}

void SoundFontVoice::reset() noexcept
{
    active = false;
    font = nullptr;
    region = nullptr;
    amplitude = {};
    modulation = {};
    phase = 0.0;
    age = 0;
    samplesUntilStart = 0;
    samplesUntilRelease = -1;
    filterLeft.reset();
    filterRight.reset();
}

void SoundFontVoice::start (const SoundFontData& f, const SoundFontRegion& r, int notePitch,
                            float velocity, const SoundFontSettings& settings, int durationSamples,
                            int startOffset) noexcept
{
    font = &f;
    region = &r;
    pitch = notePitch;
    exclusiveClass = r.exclusiveClass;

    phase = 0.0;
    age = 0;
    active = true;

    samplesUntilStart = juce::jmax (0, startOffset);
    samplesUntilRelease = durationSamples > 0 ? durationSamples : -1;

    // Pitch. scaleTuning is cents per key, so a region declaring 0 plays every
    // key at the sample's own pitch - which is how a drum kit is written.
    const auto keyCents = (double) (notePitch - r.rootKey) * (double) r.scaleTuning;
    const auto cents = keyCents + (double) r.tuneCents + (double) settings.tuneCents
                       + (double) settings.transposeSemitones * 100.0;

    increment = std::pow (2.0, cents / 1200.0) * r.sampleRate / engineSampleRate;

    // Velocity drives attenuation the way the format says - forty decibels
    // between silence and full - scaled by how much of that the channel wants.
    const auto v = juce::jlimit (0.0f, 1.0f, velocity);
    const auto sensitivity = juce::jlimit (0.0f, 1.0f, settings.velocitySensitivity);
    const auto velocityGain = 1.0f - sensitivity * (1.0f - v * v);

    gain = r.gain * velocityGain;

    // Constant power, the same law the mixer pans by, so a region panned hard
    // is not also quieter than one panned centre.
    const auto angle = (juce::jlimit (-1.0f, 1.0f, r.pan) + 1.0f) * 0.25f
                       * juce::MathConstants<float>::pi;
    leftGain = std::cos (angle);
    rightGain = std::sin (angle);

    amplitude.start (r.volumeEnvelope, engineSampleRate, settings.attackScale,
                     settings.releaseScale);
    modulation.start (r.modEnvelope, engineSampleRate, 1.0f, 1.0f);

    filterBaseCents = juce::jlimit (1500.0f, 13500.0f,
                                    1200.0f * std::log2 (r.filterCutoffHz / 8.176f)
                                        + settings.filterOffsetCents);
    modToFilterCents = r.modEnvToFilterCents;
    filterQDb = r.filterQ;

    // Wide open and unmodulated is a filter that does nothing, and running it
    // would only cost the mix a biquad's rounding on every sample.
    filtered = filterBaseCents < 13400.0f || std::abs (modToFilterCents) > 1.0f
               || filterQDb > 0.01f;

    filterLeft.reset();
    filterRight.reset();
    updateFilter();
}

void SoundFontVoice::release() noexcept
{
    amplitude.release();
    modulation.release();

    // A region that loops only while held plays out to its end from here.
    samplesUntilRelease = -1;
}

void SoundFontVoice::cut() noexcept
{
    active = false;
    amplitude = {};
    modulation = {};
}

void SoundFontVoice::updateFilter() noexcept
{
    if (! filtered)
        return;

    const auto cents = juce::jlimit (1500.0f, 13500.0f,
                                     filterBaseCents + modToFilterCents * modulation.level);
    const auto hz = 8.176f * std::pow (2.0f, cents / 1200.0f);

    filterLeft.setLowPass (engineSampleRate, hz, filterQDb);
    filterRight.setLowPass (engineSampleRate, hz, filterQDb);
}

void SoundFontVoice::renderAdd (float* left, float* right, int numSamples) noexcept
{
    if (! active || font == nullptr || region == nullptr)
        return;

    const auto* pcm = font->pcm.data();
    const auto poolSize = (double) font->pcm.size();

    const auto regionStart = (double) region->start;
    const auto regionEnd = (double) region->end;
    const auto loopStart = (double) region->loopStart;
    const auto loopEnd = (double) region->loopEnd;
    const auto loopLength = loopEnd - loopStart;

    const auto looping = region->loop != SoundFontLoop::none && loopLength > 1.0;

    // The filter tracks the modulation envelope in short runs rather than once a
    // block: a block is a tenth of a second's worth of sweep at the sizes an
    // offline render uses, and stepping the cutoff that coarsely is audible.
    constexpr int kFilterUpdateInterval = 32;
    int untilFilterUpdate = 0;

    for (int i = 0; i < numSamples; ++i)
    {
        if (samplesUntilStart > 0)
        {
            --samplesUntilStart;
            continue;
        }

        if (samplesUntilRelease == 0)
            release();

        if (samplesUntilRelease > 0)
            --samplesUntilRelease;

        if (untilFilterUpdate-- <= 0)
        {
            updateFilter();
            untilFilterUpdate = kFilterUpdateInterval;
        }

        const auto envelope = amplitude.nextSample();
        modulation.nextSample();

        if (amplitude.isFinished())
        {
            active = false;
            return;
        }

        auto position = regionStart + phase;

        if (looping && position >= loopEnd)
        {
            // A modulo rather than a subtract: a region pitched two octaves up
            // can pass the loop end more than once in a single sample step.
            phase = loopStart - regionStart + std::fmod (position - loopStart, loopLength);
            position = regionStart + phase;
        }
        else if (! looping && position >= regionEnd - 1.0)
        {
            active = false;
            return;
        }

        // The pool bound is belt and braces: the loader already clamped every
        // index into it, and a read past the end here would be a crash rather
        // than a wrong note.
        if (position < 0.0 || position + 1.0 >= poolSize)
        {
            active = false;
            return;
        }

        const auto index = (size_t) position;
        const auto fraction = (float) (position - (double) index);

        constexpr float kInt16Scale = 1.0f / 32768.0f;
        const auto a = (float) pcm[index] * kInt16Scale;
        const auto b = (float) pcm[index + 1] * kInt16Scale;
        auto sample = (a + (b - a) * fraction) * gain * envelope;

        phase += increment;

        if (filtered)
        {
            left[i] += filterLeft.processSample (sample) * leftGain;
            right[i] += filterRight.processSample (sample) * rightGain;
        }
        else
        {
            left[i] += sample * leftGain;
            right[i] += sample * rightGain;
        }
    }

    age += (juce::uint32) numSamples;
}

} // namespace dew
