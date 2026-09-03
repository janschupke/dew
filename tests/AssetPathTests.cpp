#include <catch2/catch_test_macros.hpp>

#include "model/AssetPaths.h"
#include "model/Ids.h"
#include "app/ProjectDocument.h"
#include "model/ProjectEdits.h"
#include "TestSupport.h"

using namespace dew::testing;

using namespace dew;

namespace
{

/** A temporary directory that cleans up after itself. */

void writeSomething (const juce::File& file)
{
    file.getParentDirectory().createDirectory();
    file.replaceWithText ("not really audio, but a real file");
}

/** What FileBasedDocument::saveAs does internally - write the document, then
    adopt the new file as its own. saveAs itself is gated behind
    JUCE_MODAL_LOOPS_PERMITTED, which a headless test build does not define.
*/
bool saveTo (ProjectDocument& document, const juce::File& file)
{
    if (! document.saveDocument (file).wasOk())
        return false;

    document.setFile (file);
    return true;
}

} // namespace

TEST_CASE ("the sidecar folder sits beside the project", "[assets]")
{
    const juce::File project ("/Music/Song.dew");
    REQUIRE (AssetPaths::sidecarFolderFor (project).getFileName() == "Song Assets");
    REQUIRE (AssetPaths::sidecarFolderFor (project).getParentDirectory().getFullPathName()
             == juce::File ("/Music").getFullPathName());
}

TEST_CASE ("a path inside the project folder is stored relative", "[assets]")
{
    TempDir temp { "dew-assets-" };

    const auto project = temp.dir.getChildFile ("Song.dew");
    const auto audio = temp.dir.getChildFile ("Song Assets").getChildFile ("Take 001.wav");
    writeSomething (audio);

    const auto stored = AssetPaths::relativise (audio, project);

    REQUIRE (! juce::File::isAbsolutePath (stored));
    REQUIRE (stored.contains ("Take 001.wav"));

    // And back again, which is the property that actually matters.
    REQUIRE (AssetPaths::resolve (stored, project) == audio);
}

TEST_CASE ("a path outside the project folder stays absolute", "[assets]")
{
    TempDir temp { "dew-assets-" };
    TempDir elsewhere { "dew-assets-" };

    const auto project = temp.dir.getChildFile ("Song.dew");
    const auto audio = elsewhere.dir.getChildFile ("Imported.wav");
    writeSomething (audio);

    const auto stored = AssetPaths::relativise (audio, project);

    // Rewriting it as a chain of "../" would be portable to nothing.
    REQUIRE (juce::File::isAbsolutePath (stored));
    REQUIRE (AssetPaths::resolve (stored, project) == audio);
}

TEST_CASE ("an empty path resolves to nothing, not to the project folder", "[assets]")
{
    const juce::File project ("/Music/Song.dew");

    REQUIRE (AssetPaths::resolve ({}, project) == juce::File());
    REQUIRE (AssetPaths::relativise ({}, project).isEmpty());
}

TEST_CASE ("take names never collide", "[assets]")
{
    TempDir temp { "dew-assets-" };

    const auto first = AssetPaths::nextTakeFile (temp.dir, "Bass");
    REQUIRE (first.getFileName() == "Bass Take 001.wav");

    writeSomething (first);

    const auto second = AssetPaths::nextTakeFile (temp.dir, "Bass");
    REQUIRE (second != first);
    REQUIRE (! second.existsAsFile());
}

TEST_CASE ("saving gathers a staged recording into the sidecar", "[assets][document]")
{
    // The first save is where a take recorded into an untitled project stops
    // depending on a folder in Application Support.
    TempDir staging { "dew-assets-" };
    TempDir projectDir { "dew-assets-" };

    const auto staged = staging.dir.getChildFile ("Take 001.wav");
    writeSomething (staged);

    ProjectDocument document;
    auto channel = ProjectEdits::addAudioChannel (document.getState(), "Take", nullptr);
    ProjectEdits::setSampleSource (channel, staged.getFullPathName(), 44100, 1000, nullptr);

    const auto projectFile = projectDir.dir.getChildFile ("Song.dew");
    REQUIRE (saveTo (document, projectFile));

    const auto stored = channel.getChildWithName (ids::SAMPLE)[ids::file].toString();

    REQUIRE (! juce::File::isAbsolutePath (stored));
    REQUIRE (AssetPaths::resolve (stored, projectFile).existsAsFile());
    REQUIRE (AssetPaths::sidecarFolderFor (projectFile)
                 .getChildFile ("Take 001.wav").existsAsFile());

    // The staged original is left alone: gathering copies, and a take that is
    // still being written elsewhere must not be moved out from under anything.
    REQUIRE (staged.existsAsFile());
}

TEST_CASE ("gathering does not land on the undo stack", "[assets][document]")
{
    // Saving is not an edit. If the rewrite were undoable, one Undo after a save
    // would point the project back at a folder the user never sees.
    TempDir staging { "dew-assets-" };
    TempDir projectDir { "dew-assets-" };

    const auto staged = staging.dir.getChildFile ("Take 001.wav");
    writeSomething (staged);

    ProjectDocument document;
    auto& undo = document.getUndoManager();

    undo.beginNewTransaction ("Add audio channel");
    auto channel = ProjectEdits::addAudioChannel (document.getState(), "Take", &undo);
    ProjectEdits::setSampleSource (channel, staged.getFullPathName(), 44100, 1000, &undo);

    const auto projectFile = projectDir.dir.getChildFile ("Song.dew");
    REQUIRE (saveTo (document, projectFile));

    const auto gathered = channel.getChildWithName (ids::SAMPLE)[ids::file].toString();

    REQUIRE (undo.undo());

    // One undo took out the whole edit, not just the path rewrite.
    REQUIRE (! ProjectEdits::findChannel (document.getState(), (int) channel[ids::id]).isValid());
    REQUIRE (gathered.isNotEmpty());
}

TEST_CASE ("Save As brings the audio to the new location", "[assets][document]")
{
    TempDir staging { "dew-assets-" };
    TempDir firstHome { "dew-assets-" };
    TempDir secondHome { "dew-assets-" };

    const auto staged = staging.dir.getChildFile ("Take 001.wav");
    writeSomething (staged);

    ProjectDocument document;
    auto channel = ProjectEdits::addAudioChannel (document.getState(), "Take", nullptr);
    ProjectEdits::setSampleSource (channel, staged.getFullPathName(), 44100, 1000, nullptr);

    const auto first = firstHome.dir.getChildFile ("First.dew");
    REQUIRE (saveTo (document, first));

    const auto second = secondHome.dir.getChildFile ("Second.dew");
    REQUIRE (saveTo (document, second));

    const auto stored = channel.getChildWithName (ids::SAMPLE)[ids::file].toString();

    // Resolved against the NEW project file, and living beside it - a copy that
    // still pointed into the first project's folder is the bug this prevents.
    REQUIRE (AssetPaths::resolve (stored, second).existsAsFile());
    REQUIRE (AssetPaths::resolve (stored, second).isAChildOf (secondHome.dir));
}
