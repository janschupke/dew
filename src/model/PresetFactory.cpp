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

/** The four builders describe a SOUND and nothing else.

    A preset's name and description used to be arguments here and were English
    literals; they are catalogue rows now, and PresetFactory::presets() is the
    one place that names them - see buildFor. Naming them here as well would be
    two mentions of one fact with an enum between them, which is a mismatch a
    compiler cannot see.
*/
Preset effectPreset (const char* typeId, std::initializer_list<Value> values)
{
    return { "effect", typeId, {}, {}, {}, objectOf (values), {} };
}

/** A synth preset: up to three oscillator slots and an envelope.

// clang-format off
    Slots the caller does not give are left out, and validateState fills them
    from their defaults - which switches them OFF, because that is what a fresh
    slot is. So a one-oscillator preset really is one oscillator.
*/
Preset synthPreset (std::vector<juce::var> oscillators, std::initializer_list<Value> amp)
{
    juce::Array<juce::var> slots;

    // clang-format on
    for (auto& osc : oscillators)
        slots.add (osc);

    auto* state = new juce::DynamicObject();
    state->setProperty ("oscillators", slots);
    state->setProperty ("amp", objectOf (amp));

    return { "instrument", "synth", {}, {}, {}, juce::var (state), {} };
}

// clang-format off
Preset audioPreset (std::initializer_list<Value> sample)
{
    auto* state = new juce::DynamicObject();
    state->setProperty ("sample", objectOf (sample));

    // clang-format on
    return { "instrument", "audio", {}, {}, {}, juce::var (state), {} };
}

// clang-format off
/** A soundfont preset: the offsets, and nothing that says which font.

    The file, the bank and the program are deliberately outside a preset - the
    same line the audio instrument draws, and for the same reason. A preset
    carrying a path points at somebody else's disk, and a bank and program mean
    nothing against another font. So what a soundfont preset can honestly carry
    is how the font is BENT, which is exactly what these six knobs are.
*/
Preset soundFontPreset (std::initializer_list<Value> soundfont)
{
    auto* state = new juce::DynamicObject();
    state->setProperty ("soundfont", objectOf (soundfont));

    // clang-format on
    return { "instrument", "soundfont", {}, {}, {}, juce::var (state), {} };
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
Preset rumbleCut()    { return effectPreset ("filter",{ { &ids::filterMode, "highpass" }, { &ids::cutoff, 120.0 },
                              { &ids::resonance, 0.3 }, { &ids::mix, 1.0 } }); }

Preset warmLowPass()  { return effectPreset ("filter",{ { &ids::filterMode, "lowpass" }, { &ids::cutoff, 900.0 },
                              { &ids::resonance, 0.8 }, { &ids::mix, 1.0 } }); }

Preset squelch()      { return effectPreset ("filter",{ { &ids::filterMode, "lowpass" }, { &ids::cutoff, 400.0 },
                              { &ids::resonance, 3.6 }, { &ids::mix, 1.0 } }); }

Preset ambience()     { return effectPreset ("reverb",{ { &ids::roomSize, 0.12 }, { &ids::damping, 0.5 },
                              { &ids::width, 1.0 }, { &ids::mix, 0.12 } }); }

Preset plate()        { return effectPreset ("reverb",{ { &ids::roomSize, 0.45 }, { &ids::damping, 0.35 },
                              { &ids::width, 1.0 }, { &ids::mix, 0.28 } }); }

Preset cathedral()    { return effectPreset ("reverb",{ { &ids::roomSize, 0.92 }, { &ids::damping, 0.25 },
                              { &ids::width, 1.0 }, { &ids::mix, 0.4 } }); }

Preset slapback()     { return effectPreset ("delay",{ { &ids::delayMs, 110.0 }, { &ids::feedback, 0.12 },
                              { &ids::mix, 0.3 } }); }

Preset dubEcho()      { return effectPreset ("delay",{ { &ids::delayMs, 375.0 }, { &ids::feedback, 0.78 },
                              { &ids::mix, 0.35 } }); }

Preset doubler()      { return effectPreset ("delay",{ { &ids::delayMs, 28.0 }, { &ids::feedback, 0.0 },
                              { &ids::mix, 0.45 } }); }

Preset warmDrive()    { return effectPreset ("drive",{ { &ids::drive, 2.5 }, { &ids::outputGain, 0.85 },
                              { &ids::mix, 1.0 } }); }

Preset fuzz()         { return effectPreset ("drive",{ { &ids::drive, 28.0 }, { &ids::outputGain, 0.28 },
                              { &ids::mix, 1.0 } }); }

Preset parallelGrit() { return effectPreset ("drive",{ { &ids::drive, 16.0 }, { &ids::outputGain, 0.6 },
                              { &ids::mix, 0.35 } }); }

Preset saturate()     { return effectPreset ("distortion",{ { &ids::distortionMode, "softClip" }, { &ids::drive, 4.0 },
                              { &ids::tone, 0.45 }, { &ids::outputGain, 0.7 },
                              { &ids::mix, 1.0 } }); }

Preset crunch()       { return effectPreset ("distortion",{ { &ids::distortionMode, "hardClip" }, { &ids::drive, 14.0 },
                              { &ids::tone, 0.8 }, { &ids::outputGain, 0.35 },
                              { &ids::mix, 1.0 } }); }

Preset ringFold()     { return effectPreset ("distortion",{ { &ids::distortionMode, "fold" }, { &ids::drive, 9.0 },
                              { &ids::tone, 0.6 }, { &ids::outputGain, 0.5 },
                              { &ids::mix, 1.0 } }); }

Preset subtleWiden()  { return effectPreset ("chorus",{ { &ids::rate, 0.35 }, { &ids::depth, 0.18 },
                              { &ids::mix, 0.4 } }); }

Preset classicChorus(){ return effectPreset ("chorus",{ { &ids::rate, 1.2 }, { &ids::depth, 0.4 },
                              { &ids::mix, 0.5 } }); }

Preset vibrato()      { return effectPreset ("chorus",{ { &ids::rate, 5.5 }, { &ids::depth, 0.5 },
                              { &ids::mix, 1.0 } }); }

Preset slowSweep()    { return effectPreset ("phaser",{ { &ids::rate, 0.2 }, { &ids::depth, 0.6 },
                              { &ids::centreFreq, 500.0 }, { &ids::feedback, 0.3 },
                              { &ids::mix, 0.5 } }); }

Preset jetPhaser()    { return effectPreset ("phaser",{ { &ids::rate, 0.6 }, { &ids::depth, 0.9 },
                              { &ids::centreFreq, 1800.0 }, { &ids::feedback, 0.85 },
                              { &ids::mix, 0.7 } }); }

Preset shimmer()      { return effectPreset ("phaser",{ { &ids::rate, 4.0 }, { &ids::depth, 0.25 },
                              { &ids::centreFreq, 900.0 }, { &ids::feedback, 0.15 },
                              { &ids::mix, 0.3 } }); }

Preset glue()         { return effectPreset ("compressor",{ { &ids::threshold, -14.0 }, { &ids::ratio, 2.0 },
                              { &ids::attackMs, 30.0 }, { &ids::releaseMs, 250.0 },
                              { &ids::makeup, 1.5 }, { &ids::mix, 1.0 } }); }

Preset punch()        { return effectPreset ("compressor",{ { &ids::threshold, -20.0 }, { &ids::ratio, 4.0 },
                              { &ids::attackMs, 25.0 }, { &ids::releaseMs, 80.0 },
                              { &ids::makeup, 4.0 }, { &ids::mix, 1.0 } }); }

Preset squash()       { return effectPreset ("compressor",{ { &ids::threshold, -30.0 }, { &ids::ratio, 12.0 },
                              { &ids::attackMs, 1.0 }, { &ids::releaseMs, 40.0 },
                              { &ids::makeup, 9.0 }, { &ids::mix, 1.0 } }); }

Preset masterCeiling(){ return effectPreset ("limiter",{ { &ids::ceiling, -0.3 }, { &ids::releaseMs, 200.0 },
                              { &ids::mix, 1.0 } }); }

Preset safetyNet()    { return effectPreset ("limiter",{ { &ids::ceiling, -6.0 }, { &ids::releaseMs, 100.0 },
                              { &ids::mix, 1.0 } }); }

Preset brickWall()    { return effectPreset ("limiter",{ { &ids::ceiling, -12.0 }, { &ids::releaseMs, 20.0 },
                              { &ids::mix, 1.0 } }); }

Preset air()          { return effectPreset ("eq",{ { &ids::lowGainDb, 0.0 }, { &ids::midGainDb, 0.0 },
                              { &ids::midFreq, 900.0 }, { &ids::highGainDb, 4.5 },
                              { &ids::mix, 1.0 } }); }

Preset scoop()        { return effectPreset ("eq",{ { &ids::lowGainDb, 2.0 }, { &ids::midGainDb, -6.0 },
                              { &ids::midFreq, 700.0 }, { &ids::highGainDb, 3.0 },
                              { &ids::mix, 1.0 } }); }

Preset telephone()    { return effectPreset ("eq",{ { &ids::lowGainDb, -18.0 }, { &ids::midGainDb, 6.0 },
                              { &ids::midFreq, 1600.0 }, { &ids::highGainDb, -14.0 },
                              { &ids::mix, 1.0 } }); }

Preset warmPad()      { return synthPreset ({ classicOsc ("saw", 0, -7.0, 0.5),
                              classicOsc ("saw", 0, 7.0, 0.5),
                              classicOsc ("sine", -1, 0.0, 0.35) },
                            { { &ids::attack, 0.9 }, { &ids::decay, 1.2 },
                              { &ids::sustain, 0.75 }, { &ids::release, 1.6 } }); }

Preset subBass()      { return synthPreset ({ classicOsc ("sine", -1, 0.0, 0.9), oscOff(), oscOff() },
                            { { &ids::attack, 0.004 }, { &ids::decay, 0.25 },
                              { &ids::sustain, 0.85 }, { &ids::release, 0.09 } }); }

Preset pluck()        { return synthPreset ({ classicOsc ("saw", 0, 0.0, 0.7),
                              classicOsc ("square", 0, 9.0, 0.35),
                              oscOff() },
                            { { &ids::attack, 0.001 }, { &ids::decay, 0.28 },
                              { &ids::sustain, 0.0 }, { &ids::release, 0.12 } }); }

Preset hollowKeys()   { return synthPreset ({ classicOsc ("square", 0, 0.0, 0.55),
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
    return synthPreset ({ osc, oscOff(), oscOff() },
                        { { &ids::attack, 0.02 }, { &ids::decay, 1.5 },
                          { &ids::sustain, 0.5 }, { &ids::release, 0.8 } });
}

Preset loopedBed()    { return audioPreset ({ { &ids::fadeInMs, 40.0 }, { &ids::fadeOutMs, 40.0 },
                              { &ids::transpose, 0.0 }, { &ids::reverse, false },
                              { &ids::loop, true } }); }

Preset reverseSwell() { return audioPreset ({ { &ids::fadeInMs, 600.0 }, { &ids::fadeOutMs, 20.0 },
                              { &ids::transpose, 0.0 }, { &ids::reverse, true },
                              { &ids::loop, false } }); }

Preset softenedFont()  { return soundFontPreset ({ { &ids::transpose, 0.0 }, { &ids::tuneCents, 0.0 },
                              { &ids::filterOffset, -600.0 }, { &ids::attackScale, 2.5 },
                              { &ids::releaseScale, 2.0 }, { &ids::velocitySens, 1.0 } }); }

Preset steppedFont()   { return soundFontPreset ({ { &ids::transpose, 0.0 }, { &ids::tuneCents, 0.0 },
                              { &ids::filterOffset, 0.0 }, { &ids::attackScale, 1.0 },
                              { &ids::releaseScale, 0.4 }, { &ids::velocitySens, 0.0 } }); }

} // namespace

Preset PresetFactory::buildFor (const Entry& entry)
{
    auto preset = entry.build();

    // The REFERENCE locale, because this is what goes INTO the file and the
    // file is compared byte for byte against a committed copy. What a person
    // reads comes from PresetLibrary::displayName, in their own locale, off the
    // same entry.
    preset.name = trIn (referenceLocale(), entry.name);
    preset.description = trIn (referenceLocale(), entry.description);

    // The category is NOT locale-dependent the way the two above are: it goes
    // into the file as its stable id, so the file says the same thing whatever
    // language wrote it.
    preset.category = entry.category;

    return preset;
}

const std::vector<PresetFactory::Entry>& PresetFactory::presets()
{
    static const std::vector<Entry> all {
        { "rumble-cut.dewpreset", StringId::preset_rumbleCut_name,
          StringId::preset_rumbleCut_description, PresetCategory::gentle, &rumbleCut },
        { "warm-low-pass.dewpreset", StringId::preset_warmLowPass_name,
          StringId::preset_warmLowPass_description, PresetCategory::character, &warmLowPass },
        { "squelch.dewpreset", StringId::preset_squelch_name,
          StringId::preset_squelch_description, PresetCategory::extreme, &squelch },
        { "ambience.dewpreset", StringId::preset_ambience_name,
          StringId::preset_ambience_description, PresetCategory::gentle, &ambience },
        { "plate.dewpreset", StringId::preset_plate_name,
          StringId::preset_plate_description, PresetCategory::character, &plate },
        { "cathedral.dewpreset", StringId::preset_cathedral_name,
          StringId::preset_cathedral_description, PresetCategory::extreme, &cathedral },
        { "slapback.dewpreset", StringId::preset_slapback_name,
          StringId::preset_slapback_description, PresetCategory::character, &slapback },
        { "dub-echo.dewpreset", StringId::preset_dubEcho_name,
          StringId::preset_dubEcho_description, PresetCategory::extreme, &dubEcho },
        { "doubler.dewpreset", StringId::preset_doubler_name,
          StringId::preset_doubler_description, PresetCategory::gentle, &doubler },
        { "warm.dewpreset", StringId::preset_warmDrive_name,
          StringId::preset_warmDrive_description, PresetCategory::gentle, &warmDrive },
        { "fuzz.dewpreset", StringId::preset_fuzz_name,
          StringId::preset_fuzz_description, PresetCategory::extreme, &fuzz },
        { "parallel-grit.dewpreset", StringId::preset_parallelGrit_name,
          StringId::preset_parallelGrit_description, PresetCategory::character, &parallelGrit },
        { "saturate.dewpreset", StringId::preset_saturate_name,
          StringId::preset_saturate_description, PresetCategory::gentle, &saturate },
        { "crunch.dewpreset", StringId::preset_crunch_name,
          StringId::preset_crunch_description, PresetCategory::character, &crunch },
        { "ring-fold.dewpreset", StringId::preset_ringFold_name,
          StringId::preset_ringFold_description, PresetCategory::extreme, &ringFold },
        { "subtle-widen.dewpreset", StringId::preset_subtleWiden_name,
          StringId::preset_subtleWiden_description, PresetCategory::gentle, &subtleWiden },
        { "classic-chorus.dewpreset", StringId::preset_classicChorus_name,
          StringId::preset_classicChorus_description, PresetCategory::character, &classicChorus },
        { "vibrato.dewpreset", StringId::preset_vibrato_name,
          StringId::preset_vibrato_description, PresetCategory::extreme, &vibrato },
        { "slow-sweep.dewpreset", StringId::preset_slowSweep_name,
          StringId::preset_slowSweep_description, PresetCategory::character, &slowSweep },
        { "jet.dewpreset", StringId::preset_jetPhaser_name,
          StringId::preset_jetPhaser_description, PresetCategory::extreme, &jetPhaser },
        { "shimmer.dewpreset", StringId::preset_shimmer_name,
          StringId::preset_shimmer_description, PresetCategory::gentle, &shimmer },
        { "air.dewpreset", StringId::preset_air_name,
          StringId::preset_air_description, PresetCategory::gentle, &air },
        { "scoop.dewpreset", StringId::preset_scoop_name,
          StringId::preset_scoop_description, PresetCategory::character, &scoop },
        { "telephone.dewpreset", StringId::preset_telephone_name,
          StringId::preset_telephone_description, PresetCategory::extreme, &telephone },
        { "glue.dewpreset", StringId::preset_glue_name,
          StringId::preset_glue_description, PresetCategory::gentle, &glue },
        { "punch.dewpreset", StringId::preset_punch_name,
          StringId::preset_punch_description, PresetCategory::character, &punch },
        { "squash.dewpreset", StringId::preset_squash_name,
          StringId::preset_squash_description, PresetCategory::extreme, &squash },
        { "master-ceiling.dewpreset", StringId::preset_masterCeiling_name,
          StringId::preset_masterCeiling_description, PresetCategory::character, &masterCeiling },
        { "safety-net.dewpreset", StringId::preset_safetyNet_name,
          StringId::preset_safetyNet_description, PresetCategory::gentle, &safetyNet },
        { "brick-wall.dewpreset", StringId::preset_brickWall_name,
          StringId::preset_brickWall_description, PresetCategory::extreme, &brickWall },
        { "warm-pad.dewpreset", StringId::preset_warmPad_name,
          StringId::preset_warmPad_description, PresetCategory::pads, &warmPad },
        { "sub-bass.dewpreset", StringId::preset_subBass_name,
          StringId::preset_subBass_description, PresetCategory::bass, &subBass },
        { "pluck.dewpreset", StringId::preset_pluck_name,
          StringId::preset_pluck_description, PresetCategory::keys, &pluck },
        { "hollow-keys.dewpreset", StringId::preset_hollowKeys_name,
          StringId::preset_hollowKeys_description, PresetCategory::keys, &hollowKeys },
        { "morphing-sweep.dewpreset", StringId::preset_morphingSweep_name,
          StringId::preset_morphingSweep_description, PresetCategory::pads, &morphingSweep },
        { "looped-bed.dewpreset", StringId::preset_loopedBed_name,
          StringId::preset_loopedBed_description, PresetCategory::texture, &loopedBed },
        { "reverse-swell.dewpreset", StringId::preset_reverseSwell_name,
          StringId::preset_reverseSwell_description, PresetCategory::texture, &reverseSwell },
        { "softened.dewpreset", StringId::preset_softenedFont_name,
          StringId::preset_softenedFont_description, PresetCategory::texture, &softenedFont },
        { "stepped.dewpreset", StringId::preset_steppedFont_name,
          StringId::preset_steppedFont_description, PresetCategory::texture, &steppedFont },
    };

    // clang-format on
    return all;
}

} // namespace dew
