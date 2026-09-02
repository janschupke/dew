#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "Ids.h"

namespace dew
{

/** The open project.

    Subclasses juce::FileBasedDocument rather than reimplementing it: dirty
    tracking, the "save changes before closing?" prompt, Save As, and the
    recently-opened file all come from there. This class supplies the four
    document hooks and owns the ValueTree plus its UndoManager.

    All edits must go through getUndoManager() so undo/redo covers everything.
*/
class ProjectDocument : public juce::FileBasedDocument,
                        private juce::ValueTree::Listener
{
public:
    ProjectDocument();
    ~ProjectDocument() override;

    static constexpr const char* fileExtension = ".dew";
    static constexpr const char* fileWildcard  = "*.dew";

    juce::ValueTree& getState()             { return state; }
    const juce::ValueTree& getState() const { return state; }
    juce::UndoManager& getUndoManager()     { return undoManager; }

    /** Replaces the whole document. Used by File > New and by loading. Clears
        undo history, because undoing across a document swap is meaningless.
    */
    void setState (juce::ValueTree newState, bool markAsUnchanged);

    /** Warnings from the most recent load: unknown keys, wrong types, and other
        recoverable problems. Empty after a clean load.
    */
    const juce::StringArray& getLastLoadWarnings() const { return lastLoadWarnings; }

    /** Fired whenever the project changes in a way the engine must see. */
    std::function<void()> onProjectChanged;

    // --- FileBasedDocument -------------------------------------------------
    juce::String getDocumentTitle() override;
    juce::Result loadDocument (const juce::File& file) override;
    juce::Result saveDocument (const juce::File& file) override;
    juce::File getLastDocumentOpened() override;
    void setLastDocumentOpened (const juce::File& file) override;

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override;
    void valueTreeParentChanged (juce::ValueTree&) override;

    void noteChange();

    /** Copies every referenced audio file into the project's sidecar folder and
        rewrites the stored paths relative to it.

        Called from saveDocument, which makes one rule cover both cases: a first
        save moves recordings out of the staging folder, and a Save As brings
        the audio along to the new location instead of leaving the copy
        pointing back at the original project's folder.
    */
    void gatherAssetsInto (const juce::File& projectFile);

    juce::ValueTree state;
    juce::UndoManager undoManager;
    juce::StringArray lastLoadWarnings;
    juce::File lastOpened;
    bool suppressChangeNotifications = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProjectDocument)
};

} // namespace dew
