// What a preset carries, and what it refuses.
//
// Split out of PresetTests.cpp along the tags it already carried. The shared
// helpers are PresetHarness.h.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include <PresetData.h>

#include "model/GeneratorCatalog.h"
#include "model/Ids.h"
#include "model/ModuleCatalog.h"
#include "model/ModuleState.h"
#include "model/PresetFactory.h"
#include "model/PresetLibrary.h"
#include "model/PresetSerializer.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "model/ProjectSchema.h"
#include "ui/EditorState.h"
#include "ui/EffectChainComponent.h"
#include "ui/InstrumentPanel.h"

#include "PresetHarness.h"

using namespace dew;
using namespace dew::testing;
using Catch::Approx;

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
    for (const auto& key : { ids::volume, ids::pan, ids::basePitch, ids::name, ids::colour,
                             ids::mixerTrackId, ids::muted })
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
    REQUIRE (
        ProjectEdits::applyInstrumentPreset (channel, instrumentPreset ("synth", state), &undo));

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
    CHECK (generatorNodeFor (osc, ids::wave)[ids::wave].toString() == "sine");
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
                                   nullptr, TransactionName { "on" });

    REQUIRE (ProjectEdits::applyInstrumentPreset (channel, subBassPreset(), nullptr));

    CHECK ((bool) ProjectEdits::oscillatorAt (channel, 0)[ids::enabled]);
    CHECK_FALSE ((bool) ProjectEdits::oscillatorAt (channel, 1)[ids::enabled]);
    CHECK_FALSE ((bool) ProjectEdits::oscillatorAt (channel, 2)[ids::enabled]);
}

TEST_CASE ("a preset leaves a channel's identity, routing and chain alone", "[preset]")
{
    auto project = ProjectFactory::createDefault();
    auto channel = firstChannel (project);

    ProjectEdits::setProperty (channel, ids::name, "Bass", nullptr, TransactionName { "name" });
    ProjectEdits::setProperty (channel, ids::mixerTrackId, 3, nullptr, TransactionName { "route" });
    ProjectEdits::setProperty (channel, ids::volume, 0.42, nullptr, TransactionName { "level" });
    ProjectEdits::setProperty (channel, ids::basePitch, 48, nullptr, TransactionName { "pitch" });

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

    const Preset reverb { "effect", "reverb", "Cathedral", "", {}, juce::var (state), {} };

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
    const auto validated = validateState (effectDescriptor (EffectType::filter), juce::var (state),
                                          warnings);

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
    state->setProperty (ids::cutoff, 800.0); // a filter's, not a reverb's

    juce::StringArray warnings;
    const auto validated = validateState (effectDescriptor (EffectType::reverb), juce::var (state),
                                          warnings);

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
    state->setProperty ("", 0.5); // the channel group's jsonKey

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
    auto* classic = new juce::DynamicObject();
    classic->setProperty (ids::wave, "sawtooth"); // not one of the four

    auto* osc = new juce::DynamicObject();
    osc->setProperty ("classic", juce::var (classic));

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

    const auto slotClassic = (*out)[0].getDynamicObject()->getProperty ("classic");
    REQUIRE (slotClassic.getDynamicObject() != nullptr);
    CHECK (slotClassic.getDynamicObject()->getProperty (ids::wave).toString() == "saw");

    INFO ("warnings: " << warnings.joinIntoString (" | "));
    CHECK (warnings.size() >= 1);
}

TEST_CASE ("capturing a soundfont channel reads its offsets, not the defaults", "[preset]")
{
    // nodesFor named ids::SAMPLE alone when deciding whether a group hangs off
    // the channel or off its INSTRUMENT child, and SOUNDFONT hangs off the
    // channel too. So capture looked for it under INSTRUMENT, found nothing and
    // returned the schema's defaults - a preset of a sound nobody made.
    //
    // Latent only because nothing in production captures state yet, which is a
    // thing a preset system that can save stops being true of. The APPLY side
    // was generalised for this reason already.
    auto project = ProjectFactory::createDefault();
    juce::UndoManager undo;

    auto channel = ProjectEdits::addSoundFontChannel (project, "Font", &undo);
    REQUIRE (channel.isValid());

    auto node = channel.getChildWithName (ids::SOUNDFONT);
    REQUIRE (node.isValid());

    ProjectEdits::setProperty (node, ids::filterOffset, -1200.0, &undo,
                               TransactionName { "Filter" });
    ProjectEdits::setProperty (node, ids::releaseScale, 3.0, &undo, TransactionName { "Release" });

    const auto state = stateFor (instrumentDescriptor (InstrumentType::soundfont), channel);
    const auto* object = state.getDynamicObject();

    REQUIRE (object != nullptr);

    const auto* group = object->getProperty ("soundfont").getDynamicObject();
    REQUIRE (group != nullptr);

    CHECK ((double) group->getProperty (ids::filterOffset) == Catch::Approx (-1200.0));
    CHECK ((double) group->getProperty (ids::releaseScale) == Catch::Approx (3.0));
}
