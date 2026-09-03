#include "model/PresetFactory.h"

#include "model/Ids.h"
#include "model/ModuleCatalog.h"

namespace dew
{

namespace
{

/** One parameter and its value, so a preset reads as a list rather than as a
    dozen setProperty calls. The identifier is a pointer to an ids:: entry for
    the reason ParamSpec::property is one - a parameter's name is never spelled
    as a literal outside Ids.h, and a source gate enforces it. */
struct Value
{
    const juce::Identifier* property;
    juce::var value;
};

juce::var objectOf (std::initializer_list<Value> values)
{
    auto* object = new juce::DynamicObject();

    for (const auto& v : values)
        object->setProperty (*v.property, v.value);

    return juce::var (object);
}

Preset effectPreset (const char* typeId, const char* name, const char* description,
                     std::initializer_list<Value> values)
{
    return { "effect", typeId, name, description, objectOf (values) };
}

/** A synth preset: up to three oscillator slots and an envelope.

// clang-format off
    Slots the caller does not give are left out, and validateState fills them
    from their defaults - which switches them OFF, because that is what a fresh
    slot is. So a one-oscillator preset really is one oscillator.
*/
Preset synthPreset (const char* name, const char* description, std::vector<juce::var> oscillators,
                    std::initializer_list<Value> amp)
{
    juce::Array<juce::var> slots;

    // clang-format on
    for (auto& osc : oscillators)
        slots.add (osc);

    auto* state = new juce::DynamicObject();
    state->setProperty ("oscillators", slots);
    state->setProperty ("amp", objectOf (amp));

    return { "instrument", "synth", name, description, juce::var (state) };
}

// clang-format off
Preset audioPreset (const char* name, const char* description,
                    std::initializer_list<Value> sample)
{
    auto* state = new juce::DynamicObject();
    state->setProperty ("sample", objectOf (sample));

    // clang-format on
    return { "instrument", "audio", name, description, juce::var (state) };
}

/** A classic oscillator slot. Gains are well under the 0.8 a single slot
    defaults to wherever more than one is on: three at full gain is three times
    the level of everything else, which is why makeOscillatorSlot switches all
    but the first off in the first place. */
juce::var classicOsc (const char* wave, int octave, double detune, double gain)
{
    return objectOf ({ { &ids::enabled, true },
                       { &ids::wave, wave },
                       { &ids::octave, octave },
                       { &ids::detuneCents, detune },
                       { &ids::gain, gain } });
}

juce::var oscOff()
{
    return objectOf ({ { &ids::enabled, false } });
}

// --- the presets -------------------------------------------------------------
//
// Values are against the ranges the catalog declares, and each one is a
// different ANSWER rather than a different number: three per effect type is the
// smallest set that shows what a type's parameters actually do.

// clang-format off
Preset rumbleCut()    { return effectPreset ("filter", "Rumble Cut",
                            "Takes the room out from under a sound.",
                            { { &ids::filterMode, "highpass" }, { &ids::cutoff, 120.0 },
                              { &ids::resonance, 0.3 }, { &ids::mix, 1.0 } }); }

Preset warmLowPass()  { return effectPreset ("filter", "Warm Low Pass",
                            "Rolls the top off without dulling it.",
                            { { &ids::filterMode, "lowpass" }, { &ids::cutoff, 900.0 },
                              { &ids::resonance, 0.8 }, { &ids::mix, 1.0 } }); }

Preset squelch()      { return effectPreset ("filter", "Squelch",
                            "Close to self-oscillation, at the resonance the engine reaches.",
                            { { &ids::filterMode, "lowpass" }, { &ids::cutoff, 400.0 },
                              { &ids::resonance, 3.6 }, { &ids::mix, 1.0 } }); }

Preset ambience()     { return effectPreset ("reverb", "Ambience",
                            "Glue rather than an effect you can hear.",
                            { { &ids::roomSize, 0.12 }, { &ids::damping, 0.5 },
                              { &ids::width, 1.0 }, { &ids::mix, 0.12 } }); }

Preset plate()        { return effectPreset ("reverb", "Plate",
                            "Bright, short and wide, sitting behind the source.",
                            { { &ids::roomSize, 0.45 }, { &ids::damping, 0.35 },
                              { &ids::width, 1.0 }, { &ids::mix, 0.28 } }); }

Preset cathedral()    { return effectPreset ("reverb", "Cathedral",
                            "A long dark tail.",
                            { { &ids::roomSize, 0.92 }, { &ids::damping, 0.25 },
                              { &ids::width, 1.0 }, { &ids::mix, 0.4 } }); }

Preset slapback()     { return effectPreset ("delay", "Slapback",
                            "One repeat, close behind.",
                            { { &ids::delayMs, 110.0 }, { &ids::feedback, 0.12 },
                              { &ids::mix, 0.3 } }); }

Preset dubEcho()      { return effectPreset ("delay", "Dub Echo",
                            "Repeats that outlast the note.",
                            { { &ids::delayMs, 375.0 }, { &ids::feedback, 0.78 },
                              { &ids::mix, 0.35 } }); }

Preset doubler()      { return effectPreset ("delay", "Doubler",
                            "Too short to hear as an echo; it widens instead.",
                            { { &ids::delayMs, 28.0 }, { &ids::feedback, 0.0 },
                              { &ids::mix, 0.45 } }); }

Preset warmDrive()    { return effectPreset ("drive", "Warm",
                            "A little weight, and no obvious distortion.",
                            { { &ids::drive, 2.5 }, { &ids::outputGain, 0.85 },
                              { &ids::mix, 1.0 } }); }

Preset fuzz()         { return effectPreset ("drive", "Fuzz",
                            "All the way, with the output pulled back to compensate.",
                            { { &ids::drive, 28.0 }, { &ids::outputGain, 0.28 },
                              { &ids::mix, 1.0 } }); }

Preset parallelGrit() { return effectPreset ("drive", "Parallel Grit",
                            "Heavy distortion blended under the dry signal.",
                            { { &ids::drive, 16.0 }, { &ids::outputGain, 0.6 },
                              { &ids::mix, 0.35 } }); }

Preset subtleWiden()  { return effectPreset ("chorus", "Subtle Widen",
                            "Slow and shallow: stereo, rather than an effect.",
                            { { &ids::rate, 0.35 }, { &ids::depth, 0.18 },
                              { &ids::mix, 0.4 } }); }

Preset classicChorus(){ return effectPreset ("chorus", "Classic Chorus",
                            "The one everybody means.",
                            { { &ids::rate, 1.2 }, { &ids::depth, 0.4 },
                              { &ids::mix, 0.5 } }); }

Preset vibrato()      { return effectPreset ("chorus", "Vibrato",
                            "Fully wet, which is what makes it vibrato and not chorus.",
                            { { &ids::rate, 5.5 }, { &ids::depth, 0.5 },
                              { &ids::mix, 1.0 } }); }

Preset air()          { return effectPreset ("eq", "Air",
                            "A lift above the top of the mix.",
                            { { &ids::lowGainDb, 0.0 }, { &ids::midGainDb, 0.0 },
                              { &ids::midFreq, 900.0 }, { &ids::highGainDb, 4.5 },
                              { &ids::mix, 1.0 } }); }

Preset scoop()        { return effectPreset ("eq", "Scoop",
                            "Out of the way of a vocal.",
                            { { &ids::lowGainDb, 2.0 }, { &ids::midGainDb, -6.0 },
                              { &ids::midFreq, 700.0 }, { &ids::highGainDb, 3.0 },
                              { &ids::mix, 1.0 } }); }

Preset telephone()    { return effectPreset ("eq", "Telephone",
                            "Everything but the middle.",
                            { { &ids::lowGainDb, -18.0 }, { &ids::midGainDb, 6.0 },
                              { &ids::midFreq, 1600.0 }, { &ids::highGainDb, -14.0 },
                              { &ids::mix, 1.0 } }); }

Preset warmPad()      { return synthPreset ("Warm Pad",
                            "Three detuned voices under a slow attack.",
                            { classicOsc ("saw", 0, -7.0, 0.5),
                              classicOsc ("saw", 0, 7.0, 0.5),
                              classicOsc ("sine", -1, 0.0, 0.35) },
                            { { &ids::attack, 0.9 }, { &ids::decay, 1.2 },
                              { &ids::sustain, 0.75 }, { &ids::release, 1.6 } }); }

Preset subBass()      { return synthPreset ("Sub Bass",
                            "One sine an octave down, and nothing else.",
                            { classicOsc ("sine", -1, 0.0, 0.9), oscOff(), oscOff() },
                            { { &ids::attack, 0.004 }, { &ids::decay, 0.25 },
                              { &ids::sustain, 0.85 }, { &ids::release, 0.09 } }); }

Preset pluck()        { return synthPreset ("Pluck",
                            "No sustain: the decay is the whole sound.",
                            { classicOsc ("saw", 0, 0.0, 0.7),
                              classicOsc ("square", 0, 9.0, 0.35),
                              oscOff() },
                            { { &ids::attack, 0.001 }, { &ids::decay, 0.28 },
                              { &ids::sustain, 0.0 }, { &ids::release, 0.12 } }); }

Preset hollowKeys()   { return synthPreset ("Hollow Keys",
                            "A square under a triangle an octave up.",
                            { classicOsc ("square", 0, 0.0, 0.55),
                              classicOsc ("triangle", 1, -5.0, 0.3),
                              oscOff() },
                            { { &ids::attack, 0.006 }, { &ids::decay, 0.6 },
                              { &ids::sustain, 0.35 }, { &ids::release, 0.35 } }); }

// clang-format on
/** The wavetable half of the instrument, and the only preset that sets unison:
    SynthVoice reads unisonVoices and unisonDetune ONLY in wavetable mode, so a
    classic slot that asked for five voices would silently get one. */
Preset morphingSweep()
{
    auto osc = objectOf ({ { &ids::enabled, true },
                           { &ids::mode, "wavetable" },
                           { &ids::wavetable, "harmonics" },
                           { &ids::gain, 0.7 },
                           { &ids::wavePosition, 0.0 },
                           { &ids::wavePositionMod, 0.85 },
                           { &ids::wavePositionSource, "envelope" },
                           { &ids::unisonVoices, 5 },
                           { &ids::unisonDetune, 14.0 } });

    // clang-format off
    return synthPreset ("Morphing Sweep",
                        "Five unison voices morphing across the table over the note.",
                        { osc, oscOff(), oscOff() },
                        { { &ids::attack, 0.02 }, { &ids::decay, 1.5 },
                          { &ids::sustain, 0.5 }, { &ids::release, 0.8 } });
}

Preset loopedBed()    { return audioPreset ("Looped Bed",
                            "Loops with short fades at both ends, so the seam does not click.",
                            { { &ids::fadeInMs, 40.0 }, { &ids::fadeOutMs, 40.0 },
                              { &ids::transpose, 0.0 }, { &ids::reverse, false },
                              { &ids::loop, true } }); }

Preset reverseSwell() { return audioPreset ("Reverse Swell",
                            "Played backwards into a long fade in.",
                            { { &ids::fadeInMs, 600.0 }, { &ids::fadeOutMs, 20.0 },
                              { &ids::transpose, 0.0 }, { &ids::reverse, true },
                              { &ids::loop, false } }); }

} // namespace

const std::vector<PresetFactory::Entry>& PresetFactory::presets()
{
    static const std::vector<Entry> all {
        { "rumble-cut.dewpreset",     &rumbleCut },
        { "warm-low-pass.dewpreset",  &warmLowPass },
        { "squelch.dewpreset",        &squelch },
        { "ambience.dewpreset",       &ambience },
        { "plate.dewpreset",          &plate },
        { "cathedral.dewpreset",      &cathedral },
        { "slapback.dewpreset",       &slapback },
        { "dub-echo.dewpreset",       &dubEcho },
        { "doubler.dewpreset",        &doubler },
        { "warm.dewpreset",           &warmDrive },
        { "fuzz.dewpreset",           &fuzz },
        { "parallel-grit.dewpreset",  &parallelGrit },
        { "subtle-widen.dewpreset",   &subtleWiden },
        { "classic-chorus.dewpreset", &classicChorus },
        { "vibrato.dewpreset",        &vibrato },
        { "air.dewpreset",            &air },
        { "scoop.dewpreset",          &scoop },
        { "telephone.dewpreset",      &telephone },
        { "warm-pad.dewpreset",       &warmPad },
        { "sub-bass.dewpreset",       &subBass },
        { "pluck.dewpreset",          &pluck },
        { "hollow-keys.dewpreset",    &hollowKeys },
        { "morphing-sweep.dewpreset", &morphingSweep },
        { "looped-bed.dewpreset",     &loopedBed },
        { "reverse-swell.dewpreset",  &reverseSwell },
    };

    // clang-format on
    return all;
}

} // namespace dew
