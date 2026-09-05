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
#include "ui/PresetMenu.h"

#include "PresetHarness.h"
#include "RollHarness.h"

using namespace dew;
using namespace dew::testing;
using Catch::Approx;

namespace
{

/** The presets a picker offers, in menu order and without the headings.

    The seam returns exactly what the menu displays, which is what stops the
    two drifting; these tests are about WHICH presets are offered, so they read
    the labels off the rows rather than restating how a row is built.
*/
juce::StringArray labelsOf (const std::vector<PresetMenuRow>& rows)
{
    juce::StringArray labels;

    for (const auto& row : rows)
        if (! row.isHeader)
            labels.add (row.label);

    return labels;
}

/** The choice applyPresetChoice wants for the row called `label`.

    NOT its position in the menu: grouping moves rows past headings, so the two
    are different numbers and only one of them loads the right sound.
*/
int choiceFor (const std::vector<PresetMenuRow>& rows, const juce::String& label)
{
    for (const auto& row : rows)
        if (! row.isHeader && row.label == label)
            return row.presetIndex + 1;

    return 0;
}

juce::StringArray headingsOf (const std::vector<PresetMenuRow>& rows)
{
    juce::StringArray headings;

    for (const auto& row : rows)
        if (row.isHeader)
            headings.add (row.label);

    return headings;
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

    const auto rows = chain.presetMenuRowsFor (0);
    const auto offered = labelsOf (rows);

    // Exactly the reverbs, by name. NOT in the library's order any more: the
    // menu is grouped, so what a row is worth is the preset it names and the
    // index it carries, not where it sits.
    juce::StringArray expected;

    for (const auto& preset : PresetLibrary::presetsFor (EffectType::reverb))
        expected.add (preset.name);

    REQUIRE (offered.size() == expected.size());
    REQUIRE (offered.size() >= 2);

    for (const auto& name : expected)
        CHECK (offered.contains (name));

    // Each row still CARRIES the sentence - it is what the tooltip and the
    // status strip are given - but it is no longer part of the label, so the
    // menu is a list of names again.
    for (const auto& row : rows)
    {
        INFO ("row: " << row.label);
        CHECK_FALSE (row.label.contains ("\n"));

        if (! row.isHeader)
            CHECK (row.description.isNotEmpty());
    }

    // Three reverbs, one to a category, so a heading over each would label
    // nothing: this menu reads exactly as it did before the grouping existed.
    CHECK (headingsOf (rows).isEmpty());

    // A mismatch is never presented in the first place.
    for (const auto& preset : PresetLibrary::presetsFor (EffectType::delay))
        CHECK_FALSE (offered.contains (preset.name));

    const auto effect = firstChannel (document.getState()).getChildWithName (ids::EFFECT);
    REQUIRE (effect.isValid());

    REQUIRE (chain.applyPresetChoice (0, choiceFor (rows, "Cathedral")));
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

    const auto rows = panel.presetMenuRowsFor();
    const auto offered = labelsOf (rows);
    REQUIRE (offered.size() >= 2);
    CHECK (offered.contains ("Sub Bass"));

    // The audio presets are not on a synth channel's menu.
    CHECK_FALSE (offered.contains ("Looped Bed"));

    REQUIRE (panel.applyPresetChoice (choiceFor (rows, "Sub Bass")));

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

    const auto offered = labelsOf (panel.presetMenuRowsFor());

    // Which kind of channel it is decides, without the panel being told.
    CHECK (offered.contains ("Looped Bed"));
    CHECK_FALSE (offered.contains ("Sub Bass"));
}

TEST_CASE ("the synth's presets are grouped, and a category names each run", "[preset][ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto rows = presetMenuRows (PresetLibrary::presetsFor (InstrumentType::synth));

    // Five sounds over three categories, two of which hold more than one - so
    // the headings are worth their rows and they appear.
    const auto headings = headingsOf (rows);
    REQUIRE (headings.size() == 3);
    CHECK (headings.contains ("Bass"));
    CHECK (headings.contains ("Keys"));
    CHECK (headings.contains ("Pads"));

    // Every preset is still offered, and no heading is offered as one.
    CHECK (labelsOf (rows).size()
           == (int) PresetLibrary::presetsFor (InstrumentType::synth).size());

    for (const auto& row : rows)
        CHECK ((row.isHeader ? row.presetIndex == -1 : row.presetIndex >= 0));

    // A heading is followed by the presets it names, and the run under "Keys"
    // is the pair that made the grouping worth having.
    juce::StringArray underKeys;
    bool inKeys = false;

    for (const auto& row : rows)
    {
        if (row.isHeader)
            inKeys = row.label == "Keys";
        else if (inKeys)
            underKeys.add (row.label);
    }

    CHECK (underKeys == juce::StringArray { "Pluck", "Hollow Keys" });
}

TEST_CASE ("a row's index addresses the preset, not its place in the menu", "[preset][ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto presets = PresetLibrary::presetsFor (InstrumentType::synth);
    const auto rows = presetMenuRows (presets);

    // The regrouping moves rows past headings, so a row's position and the
    // index it loads are different numbers. This is the invariant that keeps
    // applyPresetChoice loading the sound the row names.
    REQUIRE (rows.size() > presets.size());

    for (const auto& row : rows)
    {
        if (row.isHeader)
            continue;

        REQUIRE (row.presetIndex >= 0);
        REQUIRE (row.presetIndex < (int) presets.size());
        INFO ("row: " << row.label);
        CHECK (row.label == PresetLibrary::displayName (presets[(size_t) row.presetIndex]));
    }
}

TEST_CASE ("a preset with no category is offered, under no heading", "[preset][ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    // What a file from a later dew looks like once it is read: a real preset
    // whose category this build does not know. It must still be loadable.
    auto presets = PresetLibrary::presetsFor (InstrumentType::synth);
    presets.push_back ({ "instrument",
                         "synth",
                         "From Elsewhere",
                         "Written by a later dew.",
                         {},
                         {},
                         "elsewhere.dewpreset" });

    const auto rows = presetMenuRows (presets);

    CHECK (labelsOf (rows).contains ("From Elsewhere"));

    // Last, and with no heading invented for it.
    REQUIRE_FALSE (rows.empty());
    CHECK (rows.back().label == "From Elsewhere");
    CHECK_FALSE (headingsOf (rows).contains ("From Elsewhere"));
}

TEST_CASE ("a preset row explains itself on both surfaces", "[preset][ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    juce::String said;
    PresetMenuItem item {
        "Cathedral", "A long dark tail.", {}, [&said] (const juce::String& help) { said = help; }
    };

    // The floating tooltip reads the component; the strip is told. One string,
    // so a row cannot explain itself in one place and not the other.
    CHECK (item.getTooltip() == "A long dark tail.");

    const auto over = eventAt (item, { 0, 0 });

    item.mouseEnter (over);
    CHECK (said == "A long dark tail.");

    item.mouseExit (over);
    CHECK (said.isEmpty());
}
