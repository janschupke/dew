#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace dew
{

/** What a region does when the read pointer reaches its loop end. */
enum class SoundFontLoop
{
    none,
    continuous,  ///< loops for as long as the note sounds
    untilRelease ///< loops while held, then plays out to the sample's end
};

/** One six-stage SF2 envelope, already converted out of the format's units.

    The file stores every stage in timecents and the sustain level in centibels
    of attenuation, both logarithmic. Converting once, here, is what keeps
    std::pow off the audio thread - a voice starting a note reads seconds and a
    linear gain, the way juce::ADSR does.

    Both the volume envelope and the modulation envelope are this shape, because
    in the format they are: the same six generators with a different offset.
*/
struct SoundFontEnvelope
{
    float delaySeconds = 0.0f;
    float attackSeconds = 0.0f;
    float holdSeconds = 0.0f;
    float decaySeconds = 0.0f;
    float sustainLevel = 1.0f; ///< 0..1 linear, NOT the file's centibels
    float releaseSeconds = 0.0f;
};

/** One sample, keyed and tuned: what a note-on actually starts.

    A REGION, not a zone, because the format's two-level zone structure is
    resolved away at load time. A preset zone carries offsets, an instrument
    zone carries absolute values, and working that out per note on the audio
    thread would be a table walk per voice. It is done once, when the file is
    read, and what survives is a flat list a note-on can scan.

    Sample indices are absolute into SoundFontData::pcm, already including the
    format's coarse and fine offset generators, and already bounds-checked
    against the pool - so a voice can read them without re-validating anything.
*/
struct SoundFontRegion
{
    int lowKey = 0, highKey = 127;
    int lowVelocity = 0, highVelocity = 127;

    juce::uint32 start = 0, end = 0;
    juce::uint32 loopStart = 0, loopEnd = 0;
    SoundFontLoop loop = SoundFontLoop::none;

    int rootKey = 60;
    float tuneCents = 0.0f;     ///< coarse, fine and the header's correction, folded
    float scaleTuning = 100.0f; ///< cents per key; 0 makes the region atonal
    double sampleRate = 44100.0;

    float gain = 1.0f; ///< linear, from initialAttenuation
    float pan = 0.0f;  ///< -1 left .. +1 right

    SoundFontEnvelope volumeEnvelope;
    SoundFontEnvelope modEnvelope;
    float modEnvToFilterCents = 0.0f;

    float filterCutoffHz = 19912.0f; ///< the format's open default, 13500 cents
    float filterQ = 0.0f;            ///< dB

    /** Non-zero means starting this region silences every sounding voice that
        shares the number. It is how a closed hi-hat cuts an open one. */
    int exclusiveClass = 0;

    bool matches (int key, int velocity) const noexcept
    {
        return key >= lowKey && key <= highKey && velocity >= lowVelocity
               && velocity <= highVelocity;
    }
};

/** One sound a person can choose, addressed the way MIDI addresses it. */
struct SoundFontPreset
{
    int bank = 0;
    int program = 0;
    juce::String name;
    std::vector<SoundFontRegion> regions;
};

/** A whole soundfont, read and resolved.

    The PCM stays as the file's own signed 16-bit rather than being widened to
    float. A general MIDI font is well over a hundred megabytes of sample data,
    and converting it would double that for nothing: a voice multiplies by
    1/32768 as it interpolates, which it has to do arithmetic for anyway.

    Handed around as shared_ptr<const> for the reason a sample buffer is - a
    snapshot on the audio thread reads it while the message thread may hand the
    same font to the next snapshot.
*/
struct SoundFontData
{
    juce::String name;
    std::vector<juce::int16> pcm;
    std::vector<SoundFontPreset> presets;

    /** The preset at a bank and program, or null. Not an index: a font's
        presets are in whatever order its author wrote them, and a channel
        stores what it was pointed AT rather than where it sat in the list. */
    const SoundFontPreset* presetFor (int bank, int program) const noexcept
    {
        for (const auto& preset : presets)
            if (preset.bank == bank && preset.program == program)
                return &preset;

        return nullptr;
    }

    /** The first preset, for a font just loaded. Every font in a real library
        turns out to hold exactly one, so this is the common case rather than a
        fallback. */
    const SoundFontPreset* firstPreset() const noexcept
    {
        return presets.empty() ? nullptr : &presets.front();
    }

    bool isValid() const noexcept
    {
        return ! presets.empty() && ! pcm.empty();
    }
};

} // namespace dew
