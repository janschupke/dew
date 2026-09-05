#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "app/Settings.h"
#include "ui/MainComponent.h"

namespace dew
{

/** The menus on the bar, by identity rather than by caption or by position.

    getMenuForIndex arrives with both, and neither is safe on its own: an index
    is renumbered by an insertion, and a caption is a translated sentence that a
    copy edit changes. This is what the dispatch compares.
*/
enum class MenuBarItem
{
    file,
    edit,
    view,
    transport,
    project,
    audio,
    demos,

    /** Off macOS only. About lives in the application menu there, which is not
        on the bar at all - so this enumerator exists on every platform, and is
        the one entry menuBarOrder carries only where there is a bar to put it
        on. titleOf answers for it either way, because -Wswitch-enum is an error
        under the ci preset and a switch that skipped it would not compile.
    */
    help
};

/** The application: the window, the menu bar, and the file lifecycle.

    New/Open/Save all go through juce::FileBasedDocument's ASYNC variants.
    JUCE_MODAL_LOOPS_PERMITTED is 0 by default in modern JUCE, so the blocking
    overloads assert; more importantly, the async ones are the only way the
    "save changes?" prompt can work without freezing the audio callback's host
    message loop.
*/
// juce::JUCEApplication already IS an ApplicationCommandTarget - inheriting it
// again makes the base ambiguous.
class DewApplication : public juce::JUCEApplication, public juce::MenuBarModel, private juce::Timer
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
    bool moreThanOneInstanceAllowed() override
    {
        return false;
    }

    void initialise (const juce::String&) override;
    void shutdown() override;
    void systemRequestedQuit() override;
    void anotherInstanceStarted (const juce::String&) override;

    // --- ApplicationCommandTarget ------------------------------------------
    ApplicationCommandTarget* getNextCommandTarget() override
    {
        return nullptr;
    }
    void getAllCommands (juce::Array<juce::CommandID>&) override;
    void getCommandInfo (juce::CommandID, juce::ApplicationCommandInfo&) override;
    bool perform (const InvocationInfo&) override;

    // --- MenuBarModel -------------------------------------------------------
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int index, const juce::String& name) override;
    void menuItemSelected (int menuItemID, int topLevelMenuIndex) override;

private:
    class MainWindow;

    /** Reads the saved session and writes it back on exit. Restoring happens
        before the window is shown, so nothing visibly jumps into place.
    */
    void restoreSession();
    void saveSession();

    /** Captures the session periodically as well as on quit.

        A quit that never reaches shutdown - a crash, or a kill - would
        otherwise lose everything since launch. PropertiesFile only touches the
        disk when a value actually changes, so this costs a few comparisons.
    */
    void timerCallback() override;

    static constexpr int autosaveIntervalMs = 4000;

    std::unique_ptr<Settings> settings;

    MainComponent* getMainComponent() const;
    ProjectDocument* getDocument() const;

    void updateWindowTitle();

    /** Draws the whole interface `scale` times larger.

        A multiplier on the PEER rather than on the type scale. dew's layout is
        a ladder of pixel sizes that a font has to fit inside, so scaling only
        the text is how a caption ends up clipped by the box it was measured
        for. This scales both, and the ladder keeps meaning what it says.

        It reaches nothing offscreen: dew_shot paints into an Image with no
        peer, so every render and every headless test stays at 1:1.
    */
    void applyUiScale (double scale);

    /** Theme, motion and interface size: the three settings that are commands,
        and are now asked for by the View menu AND by the preferences window.

        Implemented once, in DewApplicationPrefs.cpp, so the two callers cannot
        come to disagree about what any of them does. Each takes a contiguous
        run of ids in the same order as the thing it selects.
    */
    void tickViewPreference (juce::CommandID, juce::ApplicationCommandInfo&);
    bool applyViewPreference (juce::CommandID);

    /** Opens a demo as an untitled document, so saving cannot overwrite it and
        the user is asked where it should go.
    */
    void openDemo (int index);
    void chooseLanguage (int index);

    /** Fills `appleMenu`. macOS only; it is what setMacMainMenu puts at the top
        of the application menu, where a Mac user looks for About and Settings.
    */
    void buildAppleMenu();

    /** Menu ids for the Demos menu, kept clear of the command ids. */
    static constexpr int demoMenuBaseId = 0x3000;

    /** The language items, one per compiled-in locale plus "System" at the
        base. A range rather than a command each, because which languages exist
        is data - resources/i18n decides it - and a CommandID has to be an
        enumerator somebody wrote down. */
    static constexpr int languageMenuBaseId = 0x3400;

    std::unique_ptr<MainWindow> mainWindow;
    juce::ApplicationCommandManager commandManager;

    /** The application-menu items on macOS, HELD rather than built inline.

        setMacMainMenu takes a `const PopupMenu*` and getMacExtraAppleItemsMenu
        hands the same pointer back, so a temporary would be a dangling one the
        moment initialise returned. Empty and unread everywhere else.
    */
    juce::PopupMenu appleMenu;

    JUCE_DECLARE_NON_COPYABLE (DewApplication)
};

} // namespace dew
