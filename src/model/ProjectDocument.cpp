#include "model/ProjectDocument.h"

#include "model/AssetPaths.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "model/ProjectSerializer.h"

namespace dew
{

ProjectDocument::ProjectDocument()
    : FileBasedDocument (fileExtension,
                         fileWildcard,
                         "Open a dew project",
                         "Save dew project")
{
    setState (ProjectFactory::createDefault(), true);
}

ProjectDocument::~ProjectDocument()
{
    state.removeListener (this);
}

void ProjectDocument::setState (juce::ValueTree newState, bool markAsUnchanged)
{
    state.removeListener (this);

    {
        const juce::ScopedValueSetter<bool> quiet (suppressChangeNotifications, true);
        state = std::move (newState);
        state.addListener (this);
    }

    // Undo history belongs to a document, not to the application.
    undoManager.clearUndoHistory();

    if (markAsUnchanged)
        setChangedFlag (false);
    else
        changed();

    if (onProjectChanged != nullptr)
        onProjectChanged();
}

juce::String ProjectDocument::getDocumentTitle()
{
    const auto name = state[ids::name].toString();
    return name.isNotEmpty() ? name : juce::String ("Untitled");
}

juce::Result ProjectDocument::loadDocument (const juce::File& file)
{
    auto loaded = ProjectSerializer::readFromFile (file);

    if (! loaded.ok())
        return loaded.result;

    lastLoadWarnings = loaded.warnings;
    setState (loaded.tree, true);

    return juce::Result::ok();
}

juce::Result ProjectDocument::saveDocument (const juce::File& file)
{
    // Audio first: a .dew that names files it did not bring with it is the one
    // way this format can be saved and still be broken afterwards.
    gatherAssetsInto (file);

    return ProjectSerializer::writeToFile (state, file);
}

void ProjectDocument::gatherAssetsInto (const juce::File& projectFile)
{
    const auto sidecar = AssetPaths::sidecarFolderFor (projectFile);

    if (sidecar == juce::File())
        return;

    for (auto channel : state)
    {
        if (! channel.hasType (ids::CHANNEL))
            continue;

        auto sample = channel.getChildWithName (ids::SAMPLE);

        if (! sample.isValid())
            continue;

        const auto stored = sample[ids::file].toString();

        if (stored.isEmpty())
            continue;

        // Resolved against where the document lives NOW, which on a Save As is
        // still the old location - that is exactly the path we are copying from.
        const auto source = AssetPaths::resolve (stored, getFile());

        if (! source.existsAsFile())
            continue;

        if (source.isAChildOf (sidecar))
        {
            // Already in the right folder; only the stored form may need fixing,
            // which is what a Save As of an already-gathered project needs.
            sample.setProperty (ids::file, AssetPaths::relativise (source, projectFile), nullptr);
            continue;
        }

        sidecar.createDirectory();

        auto destination = sidecar.getChildFile (source.getFileName());

        // A name collision between two takes from different folders would
        // otherwise have one silently overwrite the other.
        if (destination.existsAsFile() && destination.getSize() != source.getSize())
            destination = AssetPaths::nextTakeFile (sidecar, channel[ids::name].toString());

        if (! source.copyFileTo (destination))
            continue;

        // Deliberately not through the UndoManager: gathering is a consequence
        // of saving, not an edit the user made, and landing it on the undo
        // stack would let Undo point the project back at the staging folder.
        sample.setProperty (ids::file, AssetPaths::relativise (destination, projectFile), nullptr);
    }
}

juce::File ProjectDocument::getLastDocumentOpened()
{
    return lastOpened;
}

void ProjectDocument::setLastDocumentOpened (const juce::File& file)
{
    lastOpened = file;
}

void ProjectDocument::noteChange()
{
    if (suppressChangeNotifications)
        return;

    changed();

    if (onProjectChanged != nullptr)
        onProjectChanged();
}

void ProjectDocument::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) { noteChange(); }
void ProjectDocument::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&)             { noteChange(); }
void ProjectDocument::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int)      { noteChange(); }
void ProjectDocument::valueTreeChildOrderChanged (juce::ValueTree&, int, int)              { noteChange(); }
void ProjectDocument::valueTreeParentChanged (juce::ValueTree&)                            { noteChange(); }

} // namespace dew
