#include <catch2/catch_test_macros.hpp>

#include "FixtureSoundFont.h"
#include "TestSupport.h"
#include "app/ProjectDocument.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "model/AssetPaths.h"
#include "model/ProjectFactory.h"
#include "model/ProjectSerializer.h"
#include "ui/EditorState.h"
#include "ui/InstrumentPanel.h"
#include "ui/SoundFontSection.h"

using namespace dew;
using dew::testing::SoundFontBuilder;

namespace
{

/** A project with one soundfont channel, plus a font on disk for it to find. */
struct Harness
{
    testing::TempDir directory { "dew-soundfont-ui-" };
    ProjectDocument document;
    EditorState editorState;
    SoundFontPool pool;
    juce::File font;

    Harness()
    {
        document.getState().copyPropertiesAndChildrenFrom (ProjectFactory::createDefault(),
                                                           nullptr);

        font = directory.dir.getChildFile ("Fixture.sf2");
        const auto bytes = SoundFontBuilder::minimal().build();
        font.replaceWithData (bytes.getData(), bytes.getSize());

        pool.setProjectFile (directory.dir.getChildFile ("Song.dew"));
    }

    juce::ValueTree addChannel()
    {
        auto channel = ProjectEdits::addSoundFontChannel (document.getState(), "Font", nullptr);
        editorState.setSelectedChannelId ((int) channel[ids::id]);
        return channel;
    }
};

/** What FileBasedDocument::saveAs does internally - write the document, then
    adopt the new file as its own. saveAs itself is gated behind
    JUCE_MODAL_LOOPS_PERMITTED, which a headless test build does not define.

    The order matters: gatherAssetsInto resolves against where the document
    lived BEFORE the save, which is exactly the path it is rewriting from.
*/
bool saveTo (ProjectDocument& document, const juce::File& file)
{
    if (! document.saveDocument (file).wasOk())
        return false;

    document.setFile (file);
    return true;
}

} // namespace

TEST_CASE ("loading a soundfont chooses its only sound rather than leaving the channel silent",
           "[soundfont][ui]")
{
    // Every font in a real library holds exactly one preset, so this is the
    // common case - and a channel that loaded a file and then said nothing
    // reads as a feature that does not work.
    Harness harness;
    auto channel = harness.addChannel();

    SoundFontSection section (harness.document, &harness.pool);
    section.setOwner (channel.getChildWithName (ids::SOUNDFONT));

    CHECK (section.presetMenuItems().isEmpty());

    section.loadFile (harness.font);

    CHECK (section.presetMenuItems().size() == 1);
    CHECK (section.getFileDescription() == "Fixture.sf2");

    const auto node = channel.getChildWithName (ids::SOUNDFONT);
    CHECK (node[ids::file].toString().isNotEmpty());
    CHECK (node[ids::presetName].toString() == "Ramp");
}

TEST_CASE ("a soundfont that is not on this machine says which one is missing", "[soundfont][ui]")
{
    // A soundfont is a library you own rather than a take that belongs to one
    // song, so a project arriving before its fonts is ordinary - but a channel
    // silently showing nothing is not.
    Harness harness;
    auto channel = harness.addChannel();
    ProjectEdits::setSoundFontSource (channel, "Gone.sf2", 0, 0, "Ramp", nullptr);

    SoundFontSection section (harness.document, &harness.pool);
    section.setOwner (channel.getChildWithName (ids::SOUNDFONT));

    CHECK (section.getFileDescription() == "Gone.sf2 (missing)");
    CHECK (section.presetMenuItems().isEmpty());
}

TEST_CASE ("choosing a sound changes the sound and not the file", "[soundfont][ui]")
{
    Harness harness;
    auto channel = harness.addChannel();

    // Two presets, so there is something to choose between.
    auto builder = SoundFontBuilder::minimal();
    auto second = builder.presets.front();
    second.name = "Second";
    second.program = 5;
    builder.presets.push_back (second);

    const auto bytes = builder.build();
    harness.font.replaceWithData (bytes.getData(), bytes.getSize());
    harness.pool.forget (harness.font);

    SoundFontSection section (harness.document, &harness.pool);
    section.setOwner (channel.getChildWithName (ids::SOUNDFONT));
    section.loadFile (harness.font);

    REQUIRE (section.presetMenuItems().size() == 2);

    const auto before = channel.getChildWithName (ids::SOUNDFONT)[ids::file].toString();

    REQUIRE (section.applyPresetChoice (2));

    const auto node = channel.getChildWithName (ids::SOUNDFONT);
    CHECK ((int) node[ids::program] == 5);
    CHECK (node[ids::presetName].toString() == "Second");

    // The path must not move: a list is browsed with one file loaded, and
    // rewriting it on every choice would make each of them a fresh load.
    CHECK (node[ids::file].toString() == before);
}

TEST_CASE ("the instrument panel shows a third face for a soundfont channel", "[soundfont][ui]")
{
    Harness harness;

    auto synth = ProjectEdits::addChannel (harness.document.getState(), "Synth", nullptr);
    auto font = ProjectEdits::addSoundFontChannel (harness.document.getState(), "Font", nullptr);

    InstrumentPanel panel (harness.document, harness.editorState, nullptr, &harness.pool);
    panel.setSize (320, 900);

    const auto* section = panel.findChildWithID ("soundFontSection");
    const auto* oscillators = panel.findChildWithID ("oscillatorSection");
    const auto* sample = panel.findChildWithID ("sampleSection");

    REQUIRE (section != nullptr);
    REQUIRE (oscillators != nullptr);
    REQUIRE (sample != nullptr);

    harness.editorState.setSelectedChannelId ((int) font[ids::id]);
    panel.refresh();

    CHECK (section->isVisible());
    CHECK_FALSE (oscillators->isVisible());
    CHECK_FALSE (sample->isVisible());

    // Exactly one face, and switching back must put the first one on screen -
    // three faces is where "one of them is visible" stops being automatic.
    harness.editorState.setSelectedChannelId ((int) synth[ids::id]);
    panel.refresh();

    CHECK_FALSE (section->isVisible());
    CHECK (oscillators->isVisible());
    CHECK_FALSE (sample->isVisible());
}

TEST_CASE ("Save As keeps a soundfont reference without copying the font", "[soundfont][assets]")
{
    // The decision this whole feature rests on: a soundfont is a library you
    // own, like a plugin, not a take that belongs to one song - and it can be
    // five hundred megabytes. gatherAssetsInto copies recordings; it must never
    // copy one of these.
    //
    // But it still has to REWRITE the path, which is not the same thing. A
    // relative path is relative to where the document lived when it was
    // written, so a Save As into another folder would leave it pointing at
    // nothing. The audio path never noticed, because copying into the sidecar
    // re-relativises as a side effect and this one does not copy.
    Harness harness;
    auto channel = harness.addChannel();

    const auto firstFile = harness.directory.dir.getChildFile ("Song.dew");
    REQUIRE (saveTo (harness.document, firstFile));
    harness.pool.setProjectFile (firstFile);

    SoundFontSection section (harness.document, &harness.pool);
    section.setOwner (channel.getChildWithName (ids::SOUNDFONT));
    section.loadFile (harness.font);

    // Beside the project, so it is stored relatively - which is the case that
    // breaks if the path is not rewritten.
    REQUIRE_FALSE (juce::File::isAbsolutePath (
        channel.getChildWithName (ids::SOUNDFONT)[ids::file].toString()));

    testing::TempDir elsewhere { "dew-soundfont-saveas-" };
    const auto secondFile = elsewhere.dir.getChildFile ("Song.dew");

    REQUIRE (saveTo (harness.document, secondFile));

    // Not copied.
    CHECK_FALSE (AssetPaths::sidecarFolderFor (secondFile)
                     .getChildFile (harness.font.getFileName())
                     .existsAsFile());

    // And still pointing at the font where it actually lives.
    const auto stored = channel.getChildWithName (ids::SOUNDFONT)[ids::file].toString();
    CHECK (AssetPaths::resolve (stored, secondFile) == harness.font);
    CHECK (AssetPaths::resolve (stored, secondFile).existsAsFile());
}

TEST_CASE ("a project written before soundfonts existed gains an inert node", "[soundfont][schema]")
{
    // Additive with declared defaults, which is what lets every earlier file
    // load as exactly what it was. Built by taking a real project apart rather
    // than by hand: a hand-written payload tests the reader against one
    // person's idea of the format, and misses everything else the schema wants.
    auto older = ProjectFactory::createDefault();

    for (auto channel : older)
        if (channel.hasType (ids::CHANNEL))
            channel.removeChild (channel.getChildWithName (ids::SOUNDFONT), nullptr);

    older.setProperty (ids::formatVersion, 12, nullptr);

    const auto loaded = ProjectSerializer::fromJsonString (ProjectSerializer::toJsonString (older));

    INFO (loaded.warnings.joinIntoString ("\n"));
    REQUIRE (loaded.ok());

    const auto channel = loaded.tree.getChildWithName (ids::CHANNEL);
    REQUIRE (channel.isValid());

    const auto node = channel.getChildWithName (ids::SOUNDFONT);
    REQUIRE (node.isValid());

    CHECK (node[ids::file].toString().isEmpty());
    CHECK (juce::exactlyEqual ((double) node[ids::velocitySens], 1.0));
    CHECK (ProjectEdits::playsNotes (channel));
    CHECK_FALSE (ProjectEdits::playsClips (channel));
}
