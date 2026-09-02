#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "ui/MainComponent.h"

namespace dew
{

/** The application: the window, the menu bar, and the file lifecycle.

    New/Open/Save all go through juce::FileBasedDocument's ASYNC variants.
    JUCE_MODAL_LOOPS_PERMITTED is 0 by default in modern JUCE, so the blocking
    overloads assert; more importantly, the async ones are the only way the
    "save changes?" prompt can work without freezing the audio callback's host
    message loop.
*/
// juce::JUCEApplication already IS an ApplicationCommandTarget - inheriting it
// again makes the base ambiguous.
class DewApplication : public juce::JUCEApplication,
                       public juce::MenuBarModel
{
public:
    // MainWindow is only declared here and defined in the .cpp, so BOTH of
    // these are defined there: the destructor because unique_ptr's deleter
    // needs the complete type, and the constructor because a defaulted one
    // written in-class also needs it, to unwind members if it throws.
    // JUCE_DECLARE_NON_COPYABLE below suppresses the implicit default
    // constructor that START_JUCE_APPLICATION requires, hence declaring it.
    DewApplication();
    ~DewApplication() override;

    const juce::String getApplicationName() override;
    const juce::String getApplicationVersion() override;
    bool moreThanOneInstanceAllowed() override  { return false; }

    void initialise (const juce::String&) override;
    void shutdown() override;
    void systemRequestedQuit() override;
    void anotherInstanceStarted (const juce::String&) override;

    // --- ApplicationCommandTarget ------------------------------------------
    ApplicationCommandTarget* getNextCommandTarget() override  { return nullptr; }
    void getAllCommands (juce::Array<juce::CommandID>&) override;
    void getCommandInfo (juce::CommandID, juce::ApplicationCommandInfo&) override;
    bool perform (const InvocationInfo&) override;

    // --- MenuBarModel -------------------------------------------------------
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int index, const juce::String& name) override;
    void menuItemSelected (int menuItemID, int topLevelMenuIndex) override;

private:
    class MainWindow;

    MainComponent* getMainComponent() const;
    ProjectDocument* getDocument() const;

    void updateWindowTitle();

    /** Opens a demo as an untitled document, so saving cannot overwrite it and
        the user is asked where it should go.
    */
    void openDemo (int index);

    /** Menu ids for the Demos menu, kept clear of the command ids. */
    static constexpr int demoMenuBaseId = 0x3000;

    std::unique_ptr<MainWindow> mainWindow;
    juce::ApplicationCommandManager commandManager;

    JUCE_DECLARE_NON_COPYABLE (DewApplication)
};

} // namespace dew
