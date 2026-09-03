#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "model/Ids.h"
#include "model/ModuleCatalog.h"
#include "model/ModuleState.h"
#include <PresetData.h>

#include "model/Preset.h"
#include "model/PresetFactory.h"
#include "model/PresetLibrary.h"
#include "model/PresetSerializer.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "model/ProjectSchema.h"

using namespace dew;

namespace
{

juce::ValueTree firstChannel (const juce::ValueTree& project)
{
    for (const auto& child : project)
        if (child.hasType (ids::CHANNEL))
            return child;

    return {};
}

Preset instrumentPreset (const juce::String& typeId, const juce::var& state)
{
    return { "instrument", typeId, "Test", "", state };
}

/** A synth preset that turns slot 0 into a sine and switches 1 and 2 off. */
Preset subBassPreset()
{
    auto* osc0 = new juce::DynamicObject();
    osc0->setProperty (ids::enabled, true);
    osc0->setProperty (ids::wave, "sine");
    osc0->setProperty (ids::octave, -1);
    osc0->setProperty (ids::gain, 0.9);

    auto* off = new juce::DynamicObject();
    off->setProperty (ids::enabled, false);

    juce::Array<juce::var> slots;
    slots.add (juce::var (osc0));
    slots.add (juce::var (off));
    slots.add (juce::var (new juce::DynamicObject (*off)));

    auto* amp = new juce::DynamicObject();
    amp->setProperty (ids::attack, 0.004);
    amp->setProperty (ids::sustain, 0.85);

    auto* state = new juce::DynamicObject();
    state->setProperty ("oscillators", slots);
    state->setProperty ("amp", juce::var (amp));

    return instrumentPreset ("synth", juce::var (state));
}

} // namespace

TEST_CASE ("a module's state is its declared parameters and nothing else", "[preset]")
{
    auto project = ProjectFactory::createDefault();
    auto channel = firstChannel (project);
    REQUIRE (channel.isValid());

    const auto state = stateFor (instrumentDescriptor (InstrumentType::synth), channel);
    auto* object = state.getDynamicObject();
    REQUIRE (object != nullptr);

    // The groups a preset carries, and only those.
    CHECK (object->hasProperty ("oscillators"));
    CHECK (object->hasProperty ("amp"));

    // The channel's own parameters are the instrument's, but they are not its
    // sound - a preset that set the volume would be a level jump mid-mix.
    for (const auto& key : { ids::volume, ids::pan, ids::basePitch, ids::name,
                             ids::colour, ids::mixerTrackId, ids::muted, ids::solo })
    {
        INFO ("key " << key.toString());
        CHECK_FALSE (object->hasProperty (key));
    }

    const auto* slots = object->getProperty ("oscillators").getArray();
    REQUIRE (slots != nullptr);
    REQUIRE (slots->size() == kMaxOscillators);
}

TEST_CASE ("an effect's state carries mix but not its identity", "[preset]")
{
    auto project = ProjectFactory::createDefault();
    auto channel = firstChannel (project);

    const auto effect = ProjectEdits::addEffect (project, channel, "reverb", nullptr);
    REQUIRE (effect.isValid());

    const auto state = stateFor (effectDescriptor (EffectType::reverb), effect);
    auto* object = state.getDynamicObject();
    REQUIRE (object != nullptr);

    CHECK (object->hasProperty (ids::roomSize));

    // mix is what makes two presets of one type different sounds.
    CHECK (object->hasProperty (ids::mix));

    // The id keys the slot's DSP unit in the pool, and a bypass is a mixing
    // decision rather than part of a sound.
    CHECK_FALSE (object->hasProperty (ids::id));
    CHECK_FALSE (object->hasProperty (ids::enabled));
    CHECK_FALSE (object->hasProperty (ids::type));

    // And nothing belonging to another type.
    CHECK_FALSE (object->hasProperty (ids::cutoff));
}

TEST_CASE ("a default channel's state applied to a default channel changes nothing", "[preset]")
{
    // The round-trip that proves stateFor and the applier are inverses.
    auto project = ProjectFactory::createDefault();
    auto channel = firstChannel (project);

    const auto before = channel.createCopy();
    const auto state = stateFor (instrumentDescriptor (InstrumentType::synth), channel);

    juce::UndoManager undo;
    REQUIRE (ProjectEdits::applyInstrumentPreset (channel, instrumentPreset ("synth", state), &undo));

    CHECK (channel.isEquivalentTo (before));

    // Nothing changed, so nothing was recorded.
    CHECK_FALSE (undo.canUndo());
}

TEST_CASE ("loading an instrument preset is one undo step", "[preset]")
{
    auto project = ProjectFactory::createDefault();
    auto channel = firstChannel (project);

    const auto before = channel.createCopy();

    juce::UndoManager undo;
    REQUIRE (ProjectEdits::applyInstrumentPreset (channel, subBassPreset(), &undo));

    const auto osc = ProjectEdits::oscillatorAt (channel, 0);
    CHECK (osc[ids::wave].toString() == "sine");
    CHECK ((int) osc[ids::octave] == -1);

    REQUIRE (undo.canUndo());
    undo.undo();

    // ONE step, not one per parameter.
    CHECK (channel.isEquivalentTo (before));
    CHECK_FALSE (undo.canUndo());
}

TEST_CASE ("a preset that mentions one oscillator silences the others", "[preset]")
{
    auto project = ProjectFactory::createDefault();
    auto channel = firstChannel (project);

    // Every slot on and audible first, which is the state a preset has to be
    // able to get you OUT of.
    for (int i = 0; i < kMaxOscillators; ++i)
        ProjectEdits::setProperty (ProjectEdits::oscillatorAt (channel, i), ids::enabled, true,
                                   nullptr, "on");

    REQUIRE (ProjectEdits::applyInstrumentPreset (channel, subBassPreset(), nullptr));

    CHECK ((bool) ProjectEdits::oscillatorAt (channel, 0)[ids::enabled]);
    CHECK_FALSE ((bool) ProjectEdits::oscillatorAt (channel, 1)[ids::enabled]);
    CHECK_FALSE ((bool) ProjectEdits::oscillatorAt (channel, 2)[ids::enabled]);
}

TEST_CASE ("a preset leaves a channel's identity, routing and chain alone", "[preset]")
{
    auto project = ProjectFactory::createDefault();
    auto channel = firstChannel (project);

    ProjectEdits::setProperty (channel, ids::name, "Bass", nullptr, "name");
    ProjectEdits::setProperty (channel, ids::mixerTrackId, 3, nullptr, "route");
    ProjectEdits::setProperty (channel, ids::volume, 0.42, nullptr, "level");
    ProjectEdits::setProperty (channel, ids::basePitch, 48, nullptr, "pitch");

    ProjectEdits::addEffect (project, channel, "reverb", nullptr);
    ProjectEdits::addEffect (project, channel, "delay", nullptr);
    const auto effectsBefore = ProjectEdits::countEffects (channel);

    REQUIRE (ProjectEdits::applyInstrumentPreset (channel, subBassPreset(), nullptr));

    CHECK (channel[ids::name].toString() == "Bass");
    CHECK ((int) channel[ids::mixerTrackId] == 3);
    CHECK ((double) channel[ids::volume] == Catch::Approx (0.42));
    CHECK ((int) channel[ids::basePitch] == 48);
    CHECK (ProjectEdits::countEffects (channel) == effectsBefore);
}

TEST_CASE ("a preset for one effect type is refused by another", "[preset]")
{
    auto project = ProjectFactory::createDefault();
    auto channel = firstChannel (project);

    auto filter = ProjectEdits::addEffect (project, channel, "filter", nullptr);
    REQUIRE (filter.isValid());

    const auto before = filter.createCopy();

    auto* state = new juce::DynamicObject();
    state->setProperty (ids::roomSize, 0.9);

    const Preset reverb { "effect", "reverb", "Cathedral", "", juce::var (state) };

    juce::UndoManager undo;
    CHECK_FALSE (ProjectEdits::applyEffectPreset (filter, reverb, &undo));

    // Refused, not coerced: nothing written and nothing recorded.
    CHECK (filter.isEquivalentTo (before));
    CHECK_FALSE (undo.canUndo());
}

TEST_CASE ("an instrument preset is refused by the other kind of channel", "[preset]")
{
    auto project = ProjectFactory::createDefault();
    auto channel = ProjectEdits::addAudioChannel (project, "Take", nullptr);
    REQUIRE (channel.isValid());

    CHECK_FALSE (ProjectEdits::applyInstrumentPreset (channel, subBassPreset(), nullptr));
}

TEST_CASE ("a value outside its range is clamped and reported", "[preset]")
{
    auto* state = new juce::DynamicObject();
    state->setProperty (ids::cutoff, 99999.0);
    state->setProperty (ids::resonance, -5.0);

    juce::StringArray warnings;
    const auto validated = validateState (effectDescriptor (EffectType::filter),
                                          juce::var (state), warnings);

    auto* object = validated.getDynamicObject();
    REQUIRE (object != nullptr);

    CHECK ((double) object->getProperty (ids::cutoff) == Catch::Approx (20000.0));
    CHECK ((double) object->getProperty (ids::resonance) == Catch::Approx (0.05));

    INFO ("warnings: " << warnings.joinIntoString (" | "));
    CHECK (warnings.size() == 2);
}

TEST_CASE ("a key the type does not declare is dropped and reported", "[preset]")
{
    auto* state = new juce::DynamicObject();
    state->setProperty (ids::roomSize, 0.4);
    state->setProperty (ids::cutoff, 800.0);   // a filter's, not a reverb's

    juce::StringArray warnings;
    const auto validated = validateState (effectDescriptor (EffectType::reverb),
                                          juce::var (state), warnings);

    auto* object = validated.getDynamicObject();
    REQUIRE (object != nullptr);

    CHECK (object->hasProperty (ids::roomSize));
    CHECK_FALSE (object->hasProperty (ids::cutoff));

    INFO ("warnings: " << warnings.joinIntoString (" | "));
    CHECK (warnings.size() == 1);
}

TEST_CASE ("a preset carrying the channel's own parameters is refused, not obeyed", "[preset]")
{
    // A preset written against a different idea of what a preset is. Loading
    // its volume silently would move a fader in a finished mix.
    auto* state = new juce::DynamicObject();
    state->setProperty ("", 0.5);   // the channel group's jsonKey

    juce::StringArray warnings;
    const auto validated = validateState (instrumentDescriptor (InstrumentType::synth),
                                          juce::var (state), warnings);

    auto* object = validated.getDynamicObject();
    REQUIRE (object != nullptr);
    CHECK_FALSE (object->hasProperty (juce::Identifier ("volume")));

    INFO ("warnings: " << warnings.joinIntoString (" | "));
    CHECK (warnings.size() == 1);
}

TEST_CASE ("an unreadable choice falls back rather than storing nonsense", "[preset]")
{
    auto* osc = new juce::DynamicObject();
    osc->setProperty (ids::wave, "sawtooth");   // not one of the four

    juce::Array<juce::var> slots;
    slots.add (juce::var (osc));

    auto* state = new juce::DynamicObject();
    state->setProperty ("oscillators", slots);

    juce::StringArray warnings;
    const auto validated = validateState (instrumentDescriptor (InstrumentType::synth),
                                          juce::var (state), warnings);

    // Bound before .getArray(): getProperty returns by value, so the array
    // would point into a temporary that is already gone.
    const auto oscillators = validated.getDynamicObject()->getProperty ("oscillators");
    const auto* out = oscillators.getArray();
    REQUIRE (out != nullptr);
    REQUIRE (out->size() == kMaxOscillators);

    CHECK ((*out)[0].getDynamicObject()->getProperty (ids::wave).toString() == "saw");

    INFO ("warnings: " << warnings.joinIntoString (" | "));
    CHECK (warnings.size() >= 1);
}

// --- the shipped library -----------------------------------------------------

TEST_CASE ("every shipped preset loads clean and names a type dew has", "[preset][library]")
{
    const auto& entries = PresetFactory::presets();

    // A control, so the loop below cannot pass by being empty.
    REQUIRE (entries.size() >= 20);
    REQUIRE (PresetLibrary::all().size() == entries.size());

    for (const auto& entry : entries)
    {
        INFO ("preset: " << entry.fileName);

        const auto json = PresetLibrary::jsonFor (entry.fileName);
        REQUIRE (json.isNotEmpty());

        const auto loaded = PresetSerializer::fromJsonString (json);
        INFO ("result: " << loaded.result.getErrorMessage());
        REQUIRE (loaded.ok());

        // A shipped preset carries no surprises: an unknown key, a value out of
        // range or an unreadable choice all WARN, so an empty list is the
        // assertion that every one of them is exactly what its type declares.
        INFO ("warnings: " << loaded.warnings.joinIntoString (" | "));
        REQUIRE (loaded.warnings.isEmpty());

        REQUIRE (loaded.preset.name.isNotEmpty());
        REQUIRE (loaded.preset.description.isNotEmpty());

        if (loaded.preset.isEffect())
            REQUIRE (effectTypeFor (loaded.preset.typeId).has_value());
        else
            REQUIRE (instrumentTypeFor (loaded.preset.typeId).has_value());
    }
}

TEST_CASE ("every effect type ships at least one preset", "[preset][library]")
{
    // Otherwise a picker on some card would open onto nothing, which reads as
    // broken rather than as empty.
    for (const auto& descriptor : effectDescriptors())
    {
        INFO ("effect " << descriptor.id);
        CHECK (PresetLibrary::presetsFor (descriptor.type).size() >= 2);
    }

    for (const auto& descriptor : instrumentDescriptors())
    {
        INFO ("instrument " << descriptor.id);
        CHECK (PresetLibrary::presetsFor (descriptor.type).size() >= 2);
    }
}

TEST_CASE ("a type's presets are only its own", "[preset][library]")
{
    for (const auto& descriptor : effectDescriptors())
        for (const auto& preset : PresetLibrary::presetsFor (descriptor.type))
        {
            INFO ("preset " << preset.name << " offered for " << descriptor.id);
            CHECK (preset.isEffect());
            CHECK (preset.typeId == juce::String (descriptor.id));
        }
}

TEST_CASE ("the embedded presets match the committed files", "[preset][library]")
{
    // The files under presets/ are what a reader opens; the binary is what the
    // picker offers. If they drift, one of them is a lie.
    const juce::File presets (DEW_PRESETS_DIR);

    for (const auto& entry : PresetFactory::presets())
    {
        const auto file = presets.getChildFile (entry.fileName);
        INFO ("file: " << file.getFullPathName());

        REQUIRE (file.existsAsFile());
        REQUIRE (PresetLibrary::jsonFor (entry.fileName).trim()
                     == file.loadFileAsString().trim());
    }
}

TEST_CASE ("the committed presets are byte for byte what the factory writes", "[preset][library]")
{
    // Regenerate with `dew_render --write-presets presets` when this fails.
    // Normalised the way writeToFile normalises - see the demo library's twin.
    const juce::File presets (DEW_PRESETS_DIR);

    for (const auto& entry : PresetFactory::presets())
    {
        const auto file = presets.getChildFile (entry.fileName);
        INFO ("file: " << file.getFullPathName());

        REQUIRE (file.existsAsFile());
        REQUIRE (PresetSerializer::toJsonString (entry.build()).replace ("\r\n", "\n").trim()
                     == file.loadFileAsString().replace ("\r\n", "\n").trim());
    }
}

TEST_CASE ("nothing is embedded that the factory does not declare", "[preset][library]")
{
    // Walked BOTH ways, because the CMake list and the factory table are two
    // hand-written lists of the same set. A file in one and not the other is
    // the failure this catches, by name.
    juce::StringArray declared;

    for (const auto& entry : PresetFactory::presets())
        declared.add (entry.fileName);

    juce::StringArray embedded;

    for (int i = 0; i < PresetData::namedResourceListSize; ++i)
        embedded.add (PresetData::originalFilenames[i]);

    for (const auto& name : declared)
    {
        INFO ("declared but not embedded: " << name);
        CHECK (embedded.contains (name));
    }

    for (const auto& name : embedded)
    {
        INFO ("embedded but not declared: " << name);
        CHECK (declared.contains (name));
    }
}

TEST_CASE ("a preset file written by a newer dew is refused, not half-read", "[preset][library]")
{
    auto json = PresetLibrary::jsonFor ("plate.dewpreset");
    REQUIRE (json.isNotEmpty());

    const auto newer = json.replace ("\"formatVersion\": 1", "\"formatVersion\": 99");
    REQUIRE (newer != json);

    const auto loaded = PresetSerializer::fromJsonString (newer);
    CHECK_FALSE (loaded.ok());
}

TEST_CASE ("a project file is not mistaken for a preset", "[preset][library]")
{
    // The envelope's whole job. Without the tag check a .dew would load as a
    // preset with no parameters rather than being refused.
    const auto loaded = PresetSerializer::fromJsonString (R"({"format": "dew-project"})");
    CHECK_FALSE (loaded.ok());
}

TEST_CASE ("every shipped preset actually applies to its own kind of node", "[preset][library]")
{
    auto project = ProjectFactory::createDefault();
    auto channel = firstChannel (project);
    auto audio = ProjectEdits::addAudioChannel (project, "Take", nullptr);

    for (const auto& preset : PresetLibrary::all())
    {
        INFO ("preset: " << preset.name);

        if (preset.isInstrument())
        {
            const auto target = preset.typeId == "audio" ? audio : channel;
            CHECK (ProjectEdits::applyInstrumentPreset (target, preset, nullptr));
            continue;
        }

        const auto type = effectTypeFor (preset.typeId);
        REQUIRE (type.has_value());

        auto slot = ProjectEdits::addEffect (project, channel, preset.typeId, nullptr);
        REQUIRE (slot.isValid());

        CHECK (ProjectEdits::applyEffectPreset (slot, preset, nullptr));

        ProjectEdits::removeEffect (channel, slot, nullptr);
    }
}
