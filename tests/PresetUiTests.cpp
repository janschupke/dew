// Where a preset is chosen from.
//
// Split out of PresetTests.cpp along the tags it already carried. The shared
// helpers are PresetHarness.h.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include <PresetData.h>

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

namespace
{

/** The first line of a menu row - the preset's name, without the sentence the
    picker shows under it.

    The seam returns exactly what the menu displays, which is what stops the
    two drifting; these tests are about WHICH presets are offered, so they read
    the label off the row rather than restating how a row is built.
*/
juce::StringArray labelsOf (const juce::StringArray& rows)
{
    juce::StringArray labels;

    for (const auto& row : rows)
        labels.add (row.upToFirstOccurrenceOf ("\n", false, false));

    return labels;
}

} // namespace

TEST_CASE ("an effect card offers only its own type's presets, and loads one", "[preset][ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    EditorState editorState;

    document.setState (ProjectFactory::createDefault(), true);

    EffectChainComponent chain { document, editorState };
    chain.setOwner (firstChannel (document.getState()));

    chain.addEffectOfType ("reverb");
    REQUIRE (chain.getNumSlotRows() == 1);

    const auto rows = chain.presetMenuItems (0);
    const auto offered = labelsOf (rows);

    // Exactly the reverbs, by name and in the library's order.
    juce::StringArray expected;

    for (const auto& preset : PresetLibrary::presetsFor (EffectType::reverb))
        expected.add (preset.name);

    REQUIRE (offered == expected);
    REQUIRE (offered.size() >= 2);

    // And each row carries the sentence the picker shows under the name -
    // authored, serialised into every .dewpreset, and displayed nowhere until
    // now.
    for (const auto& preset : PresetLibrary::presetsFor (EffectType::reverb))
    {
        INFO ("preset: " << preset.name);
        REQUIRE (preset.description.isNotEmpty());
        CHECK (rows.contains (preset.name + "\n" + preset.description));
    }

    // A mismatch is never presented in the first place.
    for (const auto& preset : PresetLibrary::presetsFor (EffectType::delay))
        CHECK_FALSE (offered.contains (preset.name));

    const auto effect = firstChannel (document.getState()).getChildWithName (ids::EFFECT);
    REQUIRE (effect.isValid());

    REQUIRE (chain.applyPresetChoice (0, offered.indexOf ("Cathedral") + 1));
    CHECK ((double) effect[ids::roomSize] == Catch::Approx (0.92));

    // And it is one undo step, taken through the document's own manager.
    REQUIRE (document.getUndoManager().canUndo());
    document.getUndoManager().undo();
    CHECK ((double) effect[ids::roomSize] != Catch::Approx (0.92));
}

TEST_CASE ("an out-of-range menu choice does nothing", "[preset][ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    EditorState editorState;

    document.setState (ProjectFactory::createDefault(), true);

    EffectChainComponent chain { document, editorState };
    chain.setOwner (firstChannel (document.getState()));
    chain.addEffectOfType ("reverb");

    // Adding the effect is itself an undo step, so what a refused choice must
    // leave alone is the step ALREADY there - not an empty stack.
    const auto before = document.getUndoManager().getUndoDescription();
    REQUIRE (before.isNotEmpty());

    // 0 is what showMenuAsync reports when the menu is dismissed.
    CHECK_FALSE (chain.applyPresetChoice (0, 0));
    CHECK_FALSE (chain.applyPresetChoice (0, 999));

    CHECK (document.getUndoManager().getUndoDescription() == before);
}

TEST_CASE ("the instrument panel offers the presets for the channel it is on", "[preset][ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    EditorState editorState;

    document.setState (ProjectFactory::createDefault(), true);
    editorState.setSelectedChannelId ((int) firstChannel (document.getState())[ids::id]);

    InstrumentPanel panel { document, editorState };
    panel.setSize (280, 900);
    panel.resized();

    const auto offered = labelsOf (panel.presetMenuItems());
    REQUIRE (offered.size() >= 2);
    CHECK (offered.contains ("Sub Bass"));

    // The audio presets are not on a synth channel's menu.
    CHECK_FALSE (offered.contains ("Looped Bed"));

    REQUIRE (panel.applyPresetChoice (offered.indexOf ("Sub Bass") + 1));

    const auto osc = ProjectEdits::oscillatorAt (firstChannel (document.getState()), 0);
    CHECK (osc[ids::wave].toString() == "sine");
    CHECK_FALSE (
        (bool) ProjectEdits::oscillatorAt (firstChannel (document.getState()), 1)[ids::enabled]);
}

TEST_CASE ("the panel's audio face offers the audio presets", "[preset][ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    EditorState editorState;

    document.setState (ProjectFactory::createDefault(), true);

    auto project = document.getState();
    auto audio = ProjectEdits::addAudioChannel (project, "Take", nullptr);
    editorState.setSelectedChannelId ((int) audio[ids::id]);

    InstrumentPanel panel { document, editorState };
    panel.setSize (280, 900);
    panel.resized();

    const auto offered = labelsOf (panel.presetMenuItems());

    // Which kind of channel it is decides, without the panel being told.
    CHECK (offered.contains ("Looped Bed"));
    CHECK_FALSE (offered.contains ("Sub Bass"));
}
