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

#include "model/DemoLibrary.h"
#include "model/ProjectFactory.h"
#include "ui/Hotkeys.h"
#include "ui/MainComponent.h"

namespace dew
{

// --- menu bar ----------------------------------------------------------------

juce::StringArray DewApplication::getMenuBarNames()
{
    return { "File", "Edit", "View", "Transport", "Project", "Audio", "Demos" };
}

juce::PopupMenu DewApplication::getMenuForIndex (int, const juce::String& name)
{
    // Switched on the NAME rather than the index it arrives with. Inserting the
    // View menu moved every menu after Edit along by one, and the Demos handler
    // below compared a hard-coded 5 - so the whole menu bar was one insertion
    // away from opening a demo when you asked for an audio device.
    juce::PopupMenu menu;

    if (name == "File")
    {
        menu.addCommandItem (&commandManager, CommandIDs::fileNew);
        menu.addCommandItem (&commandManager, CommandIDs::fileOpen);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, CommandIDs::fileSave);
        menu.addCommandItem (&commandManager, CommandIDs::fileSaveAs);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, CommandIDs::fileRender);
    }
    else if (name == "Edit")
    {
        menu.addCommandItem (&commandManager, CommandIDs::editUndo);
        menu.addCommandItem (&commandManager, CommandIDs::editRedo);
    }
    else if (name == "View")
    {
        menu.addCommandItem (&commandManager, CommandIDs::viewChannelRack);
        menu.addCommandItem (&commandManager, CommandIDs::viewPianoRoll);
        menu.addCommandItem (&commandManager, CommandIDs::viewPlaylist);
        menu.addCommandItem (&commandManager, CommandIDs::viewMixer);
        menu.addCommandItem (&commandManager, CommandIDs::viewScore);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, CommandIDs::viewNextTab);
        menu.addCommandItem (&commandManager, CommandIDs::viewPreviousTab);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, CommandIDs::viewToggleInstrumentPanel);
        menu.addSeparator();

        // A submenu built by walking the ids, which are contiguous and in the
        // same order as the steps - so adding a scale is one row in the enum
        // and one in the registry, and nothing here.
        juce::PopupMenu scales;

        for (int step = 0; step < Settings::numUiScaleSteps; ++step)
            scales.addCommandItem (&commandManager, CommandIDs::viewUiScaleFirst + step);

        menu.addSubMenu ("UI Scale", scales);
    }
    else if (name == "Transport")
    {
        menu.addCommandItem (&commandManager, CommandIDs::transportPlayStop);
        menu.addCommandItem (&commandManager, CommandIDs::transportRewind);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, CommandIDs::transportRecord);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, CommandIDs::transportToggleMode);
    }
    else if (name == "Project")
    {
        menu.addCommandItem (&commandManager, CommandIDs::addChannel);
        menu.addCommandItem (&commandManager, CommandIDs::addPattern);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, CommandIDs::compileScore);
    }
    else if (name == "Audio")
    {
        menu.addCommandItem (&commandManager, CommandIDs::audioSettings);
        menu.addCommandItem (&commandManager, CommandIDs::midiSettings);
    }
    else if (name == "Demos")
    {
        const auto& demos = ProjectFactory::demos();

        for (int i = 0; i < (int) demos.size(); ++i)
            menu.addItem (demoMenuBaseId + i, demos[(size_t) i].menuName);
    }

    return menu;
}

void DewApplication::menuItemSelected (int menuItemID, int topLevelMenuIndex)
{
    if (topLevelMenuIndex == getMenuBarNames().indexOf ("Demos"))
        openDemo (menuItemID - demoMenuBaseId);
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
} // namespace dew
