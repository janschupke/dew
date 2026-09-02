#include "ProjectDocument.h"

#include "ProjectFactory.h"
#include "ProjectSerializer.h"

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
    return ProjectSerializer::writeToFile (state, file);
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
