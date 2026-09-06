// =============================================================================
// The menu bar, and the demos it opens.
//
// The same class, a second translation unit beside DewApplication.cpp.
//
// Six menus and what choosing an item does. It is a table rather than logic -
// every item is a CommandID the command manager already knows, so this file
// says what appears WHERE and the perform() switch next door says what each
// one means.
//
// The file name keeps the DewApplication. prefix deliberately. src/ is not a
// library directory, so the layering gate maps a file there to dew_model and
// would refuse every ui/ include in it; the gate exempts main.cpp and anything
// starting DewApplication. by name.
// =============================================================================

#include "DewApplication.h"

#include <iterator>

#include "i18n/Strings.h"
#include "model/DemoLibrary.h"
#include "model/ProjectFactory.h"
#include "ui/Hotkeys.h"
#include "ui/MainComponent.h"

namespace dew
{

namespace
{

/** The bar's order, and the only thing the dispatch below compares.

    A separate list from the enum's own order so that moving a menu is one edit
    here rather than a renumbering, and so the order a person sees is written
    down where somebody looking for it would read it.
*/
// clang-format off
constexpr MenuBarItem menuBarOrder[] {
    MenuBarItem::file,
    MenuBarItem::edit,
    MenuBarItem::view,
    MenuBarItem::transport,
    MenuBarItem::project,
    MenuBarItem::audio,
    MenuBarItem::demos,

    // macOS has no Help menu here because About is in the application menu,
    // which is not on the bar - buildAppleMenu puts it there instead. Off
    // Apple there is no such menu, so About needs one of its own.
#if ! JUCE_MAC
    MenuBarItem::help,
#endif
};
// clang-format on

// Adding a menu is two edits, and only one of them is a compile error on its
// own. titleOf's switch has no default and the ci preset builds -Wswitch-enum
// as an error, so a new enumerator must be given a title before anything
// links. Nothing makes it appear on the BAR, though, which is this line: the
// count is the enum's, so an enumerator the order above does not carry stops
// the build here rather than going missing from the menu bar in silence.
#if JUCE_MAC
static_assert (std::size (menuBarOrder) == 7, "every MenuBarItem is on the bar exactly once");
#else
static_assert (std::size (menuBarOrder) == 8, "every MenuBarItem is on the bar exactly once");
#endif

StringId titleOf (MenuBarItem item)
{
    switch (item)
    {
        case MenuBarItem::file: return StringId::menu_file;
        case MenuBarItem::edit: return StringId::menu_edit;
        case MenuBarItem::view: return StringId::menu_view;
        case MenuBarItem::transport: return StringId::menu_transport;
        case MenuBarItem::project: return StringId::menu_project;
        case MenuBarItem::audio: return StringId::menu_audio;
        case MenuBarItem::demos: return StringId::menu_demos;
        case MenuBarItem::help: return StringId::menu_help;
    }

    return StringId::menu_file;
}

} // namespace

// --- menu bar ----------------------------------------------------------------

juce::StringArray DewApplication::getMenuBarNames()
{
    juce::StringArray names;

    for (const auto menu : menuBarOrder)
        names.add (tr (titleOf (menu)));

    return names;
}

juce::PopupMenu DewApplication::getMenuForIndex (int topLevelMenuIndex, const juce::String&)
{
    // Switched on a named POSITION, which is the only spelling that survives
    // both ways this has been wrong.
    //
    // It was a hard-coded index first: inserting the View menu moved every menu
    // after Edit along by one, and the Demos handler still compared a literal
    // 5, so the bar was one insertion away from opening a demo when you asked
    // for an audio device. It was the menu's own CAPTION next, which fixed that
    // and introduced the other one - the caption is a translated sentence now,
    // so comparing against "File" empties the File menu in every language but
    // English, and a copy edit does the same in English.
    //
    // The enum is neither. Its order is the bar's order, so an insertion is
    // still one edit, and its name is not a thing anybody translates.
    juce::PopupMenu menu;

    if (topLevelMenuIndex < 0 || topLevelMenuIndex >= (int) std::size (menuBarOrder))
        return menu;

    const auto which = menuBarOrder[(size_t) topLevelMenuIndex];

    if (which == MenuBarItem::file)
    {
        menu.addCommandItem (&commandManager, CommandIDs::fileNew);
        menu.addCommandItem (&commandManager, CommandIDs::fileOpen);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, CommandIDs::fileSave);
        menu.addCommandItem (&commandManager, CommandIDs::fileSaveAs);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, CommandIDs::fileRender);
    }
    else if (which == MenuBarItem::edit)
    {
        menu.addCommandItem (&commandManager, CommandIDs::editUndo);
        menu.addCommandItem (&commandManager, CommandIDs::editRedo);

        // Where every platform but Apple keeps it. macOS is the exception and
        // is served by buildAppleMenu, because a Mac user looks under the
        // application's own name and nowhere else.
#if ! JUCE_MAC
        menu.addSeparator();
        menu.addCommandItem (&commandManager, CommandIDs::preferences);
#endif
    }
    else if (which == MenuBarItem::view)
    {
        menu.addCommandItem (&commandManager, CommandIDs::viewChannelRack);
        menu.addCommandItem (&commandManager, CommandIDs::viewPianoRoll);
        menu.addCommandItem (&commandManager, CommandIDs::viewPlaylist);
        menu.addCommandItem (&commandManager, CommandIDs::viewMixer);
        menu.addCommandItem (&commandManager, CommandIDs::viewScore);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, CommandIDs::viewNextTab);
        menu.addCommandItem (&commandManager, CommandIDs::viewPreviousTab);
        menu.addCommandItem (&commandManager, CommandIDs::viewNextGroup);
        menu.addCommandItem (&commandManager, CommandIDs::viewPreviousGroup);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, CommandIDs::viewToggleInstrumentPanel);
        menu.addSeparator();

        // A submenu built by walking the ids, which are contiguous and in the
        // same order as the steps - so adding a scale is one row in the enum
        // and one in the registry, and nothing here.
        juce::PopupMenu scales;

        for (int step = 0; step < Settings::numUiScaleSteps; ++step)
            scales.addCommandItem (&commandManager, CommandIDs::viewUiScaleFirst + step);

        menu.addSubMenu (tr (StringId::menu_uiScale), scales);

        juce::PopupMenu motion;

        for (int step = 0; step < 3; ++step)
            motion.addCommandItem (&commandManager, CommandIDs::viewMotionFirst + step);

        menu.addSubMenu (tr (StringId::menu_motion), motion);

        juce::PopupMenu theme;

        for (int step = 0; step < 2; ++step)
            theme.addCommandItem (&commandManager, CommandIDs::viewThemeFirst + step);

        menu.addSubMenu (tr (StringId::menu_theme), theme);

        // Data-driven, the way the Demos menu is: adding a locale is a JSON
        // file and a word in CMake, and it appears here without an edit.
        juce::PopupMenu languages;
        const auto chosen = settings->getLanguage();

        languages.addItem (languageMenuBaseId, tr (StringId::menu_languageSystem), true,
                           chosen.isEmpty());

        const auto tags = availableLocales();

        for (int i = 0; i < tags.size(); ++i)
            languages.addItem (languageMenuBaseId + 1 + i, endonymOf (tags[i]), true,
                               chosen == tags[i]);

        menu.addSubMenu (tr (StringId::menu_language), languages);
    }
    else if (which == MenuBarItem::transport)
    {
        menu.addCommandItem (&commandManager, CommandIDs::transportPlayStop);
        menu.addCommandItem (&commandManager, CommandIDs::transportRewind);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, CommandIDs::transportRecord);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, CommandIDs::transportMetronome);
        menu.addCommandItem (&commandManager, CommandIDs::transportKeyboardInput);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, CommandIDs::transportToggleMode);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, CommandIDs::transportPanic);
    }
    else if (which == MenuBarItem::project)
    {
        menu.addCommandItem (&commandManager, CommandIDs::addChannel);
        menu.addCommandItem (&commandManager, CommandIDs::addPattern);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, CommandIDs::compileScore);
    }
    else if (which == MenuBarItem::audio)
    {
        menu.addCommandItem (&commandManager, CommandIDs::audioSettings);
        menu.addCommandItem (&commandManager, CommandIDs::midiSettings);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, CommandIDs::mcpSettings);
    }
    else if (which == MenuBarItem::help)
    {
        menu.addCommandItem (&commandManager, CommandIDs::about);
    }
    else if (which == MenuBarItem::demos)
    {
        const auto& demos = ProjectFactory::demos();

        // The NAME alone. A menuRow packs a sentence under the label with a
        // newline, which DewLookAndFeel::drawPopupMenuItem knows how to draw -
        // and the menu bar on macOS is the NATIVE one (setMacMainMenu), whose
        // items are NSMenuItems that no look and feel of dew's ever paints. So
        // the second line arrived as a newline in a title AppKit lays out
        // itself, and what a person read was the description.
        for (int i = 0; i < (int) demos.size(); ++i)
            menu.addItem (demoMenuBaseId + i, tr (demos[(size_t) i].menuName));
    }

    return menu;
}

void DewApplication::menuItemSelected (int menuItemID, int topLevelMenuIndex)
{
    if (topLevelMenuIndex < 0 || topLevelMenuIndex >= (int) std::size (menuBarOrder))
        return;

    if (menuBarOrder[(size_t) topLevelMenuIndex] == MenuBarItem::demos)
    {
        openDemo (menuItemID - demoMenuBaseId);
        return;
    }

    if (menuItemID >= languageMenuBaseId
        && menuItemID <= languageMenuBaseId + availableLocales().size())
        chooseLanguage (menuItemID - languageMenuBaseId);
}

void DewApplication::buildAppleMenu()
{
    // Cleared first: initialise runs once, but a member that is filled rather
    // than constructed should not depend on that to stay one menu long.
    appleMenu.clear();

#if JUCE_MAC
    // The order macOS itself uses, above the Services entry JUCE appends: what
    // this application IS, then how it is configured. Command items, so the
    // command manager invokes them and shows their keys - and so menuItemSelected
    // has nothing to do, which is why it can go on returning for a negative
    // topLevelMenuIndex, the index an apple-menu item arrives with.
    appleMenu.addCommandItem (&commandManager, CommandIDs::about);
    appleMenu.addSeparator();
    appleMenu.addCommandItem (&commandManager, CommandIDs::preferences);
#endif
}

void DewApplication::openDemo (int index)
{
    auto* document = getDocument();
    auto* main = getMainComponent();

    if (document == nullptr || main == nullptr)
        return;

    // Same courtesy as File > New: never discard unsaved work without asking.
    document->saveIfNeededAndUserAgreesAsync (
        [this, document, main, index] (juce::FileBasedDocument::SaveResult result)
        {
            if (result != juce::FileBasedDocument::savedOk)
                return;

            juce::StringArray warnings;
            auto project = DemoLibrary::load (index, warnings);

            if (! project.isValid())
            {
                main->showLoadWarnings (warnings);
                return;
            }

            // Untitled on purpose: a demo opened and edited must not be
            // saveable straight back over the one the next person opens.
            document->setState (std::move (project), false);
            document->setFile ({});

            main->documentWasReplaced();
            updateWindowTitle();
            commandManager.commandStatusChanged();
            main->showLoadWarnings (warnings);
        });
}
void DewApplication::chooseLanguage (int index)
{
    // Stored and not applied. setLocale rebuilds the table tr() hands out
    // references into, so switching under a live interface would leave every
    // label already built pointing into the table that was replaced. The status
    // line says so rather than the change happening silently and partly.
    const auto tags = availableLocales();

    settings->setLanguage (index == 0 ? juce::String() : tags[index - 1]);

    if (auto* main = getMainComponent())
        main->showLanguageNotice();
}

} // namespace dew
