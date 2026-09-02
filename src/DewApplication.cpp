#include "DewApplication.h"

#include "BuildInfo.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "ui/Commands.h"

namespace dew
{

class DewApplication::MainWindow : public juce::DocumentWindow
{
public:
    MainWindow (const juce::String& name, juce::ApplicationCommandManager& manager)
        : DocumentWindow (name, Palette::background, DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new MainComponent(), true);
        setResizable (true, false);
        setResizeLimits (900, 560, 20000, 20000);
        centreWithSize (getWidth(), getHeight());

        addKeyListener (manager.getKeyMappings());
        setVisible (true);
    }

    MainComponent* getMainComponent() const
    {
        return dynamic_cast<MainComponent*> (getContentComponent());
    }

    void closeButtonPressed() override
    {
        JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};

// -----------------------------------------------------------------------------

DewApplication::DewApplication() = default;
DewApplication::~DewApplication() = default;

const juce::String DewApplication::getApplicationName()    { return "dew"; }
const juce::String DewApplication::getApplicationVersion() { return BuildInfo::version(); }

void DewApplication::initialise (const juce::String&)
{
    mainWindow = std::make_unique<MainWindow> (getApplicationName(), commandManager);

    commandManager.registerAllCommandsForTarget (this);
    commandManager.setFirstCommandTarget (this);

    juce::MenuBarModel::setMacMainMenu (this);
    updateWindowTitle();
}

void DewApplication::shutdown()
{
    juce::MenuBarModel::setMacMainMenu (nullptr);
    mainWindow.reset();
}

void DewApplication::anotherInstanceStarted (const juce::String&) {}

MainComponent* DewApplication::getMainComponent() const
{
    return mainWindow != nullptr ? mainWindow->getMainComponent() : nullptr;
}

ProjectDocument* DewApplication::getDocument() const
{
    if (auto* main = getMainComponent())
        return &main->getDocument();

    return nullptr;
}

void DewApplication::updateWindowTitle()
{
    if (mainWindow == nullptr)
        return;

    auto* document = getDocument();

    if (document == nullptr)
        return;

    const auto file = document->getFile();
    const auto name = file != juce::File() ? file.getFileNameWithoutExtension()
                                           : document->getDocumentTitle();

    mainWindow->setName ("dew — " + name + (document->hasChangedSinceSaved() ? " •" : ""));
}

void DewApplication::systemRequestedQuit()
{
    auto* document = getDocument();

    if (document == nullptr)
    {
        quit();
        return;
    }

    // Async: a modal loop here would block the message thread the audio device
    // callbacks and the UI both depend on.
    // JUCEApplication::quit() is static, so this lambda captures nothing.
    document->saveIfNeededAndUserAgreesAsync ([] (juce::FileBasedDocument::SaveResult result)
    {
        if (result == juce::FileBasedDocument::savedOk)
            quit();
    });
}

// --- commands ----------------------------------------------------------------

void DewApplication::getAllCommands (juce::Array<juce::CommandID>& commands)
{
    commands.addArray ({ CommandIDs::fileNew, CommandIDs::fileOpen, CommandIDs::fileSave,
                         CommandIDs::fileSaveAs, CommandIDs::editUndo, CommandIDs::editRedo,
                         CommandIDs::transportPlayStop, CommandIDs::transportRewind,
                         CommandIDs::transportToggleMode,
                         CommandIDs::addChannel, CommandIDs::addPattern });
}

void DewApplication::getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    auto* document = getDocument();
    auto* main = getMainComponent();

    switch (id)
    {
        case CommandIDs::fileNew:
            info.setInfo ("New", "Start an empty project", "File", 0);
            info.addDefaultKeypress ('n', juce::ModifierKeys::commandModifier);
            break;

        case CommandIDs::fileOpen:
            info.setInfo ("Open...", "Open a dew project", "File", 0);
            info.addDefaultKeypress ('o', juce::ModifierKeys::commandModifier);
            break;

        case CommandIDs::fileSave:
            info.setInfo ("Save", "Save this project", "File", 0);
            info.addDefaultKeypress ('s', juce::ModifierKeys::commandModifier);
            info.setActive (document != nullptr);
            break;

        case CommandIDs::fileSaveAs:
            info.setInfo ("Save As...", "Save this project to a new file", "File", 0);
            info.addDefaultKeypress ('s', juce::ModifierKeys::commandModifier
                                              | juce::ModifierKeys::shiftModifier);
            info.setActive (document != nullptr);
            break;

        case CommandIDs::editUndo:
            info.setInfo ("Undo", "Undo the last edit", "Edit", 0);
            info.addDefaultKeypress ('z', juce::ModifierKeys::commandModifier);
            info.setActive (document != nullptr && document->getUndoManager().canUndo());
            break;

        case CommandIDs::editRedo:
            info.setInfo ("Redo", "Redo the last undone edit", "Edit", 0);
            info.addDefaultKeypress ('z', juce::ModifierKeys::commandModifier
                                              | juce::ModifierKeys::shiftModifier);
            info.setActive (document != nullptr && document->getUndoManager().canRedo());
            break;

        case CommandIDs::transportPlayStop:
            info.setInfo ("Play / Stop", "Start or stop playback", "Transport", 0);
            info.addDefaultKeypress (juce::KeyPress::spaceKey, 0);
            info.setActive (main != nullptr);
            break;

        case CommandIDs::transportRewind:
            info.setInfo ("Rewind", "Return the playhead to the start", "Transport", 0);
            info.addDefaultKeypress (juce::KeyPress::homeKey, 0);
            info.setActive (main != nullptr);
            break;

        case CommandIDs::transportToggleMode:
            info.setInfo ("Toggle Pattern / Song", "Switch between pattern and song playback",
                          "Transport", 0);
            info.addDefaultKeypress ('l', juce::ModifierKeys::commandModifier);
            info.setActive (main != nullptr);
            break;

        case CommandIDs::addChannel:
            info.setInfo ("Add Channel", "Add a new instrument channel", "Project", 0);
            info.addDefaultKeypress ('k', juce::ModifierKeys::commandModifier);
            info.setActive (document != nullptr);
            break;

        case CommandIDs::addPattern:
            info.setInfo ("Add Pattern", "Add a new pattern", "Project", 0);
            info.addDefaultKeypress ('p', juce::ModifierKeys::commandModifier
                                              | juce::ModifierKeys::shiftModifier);
            info.setActive (document != nullptr);
            break;

        default:
            break;
    }
}

bool DewApplication::perform (const InvocationInfo& info)
{
    auto* document = getDocument();
    auto* main = getMainComponent();

    if (document == nullptr || main == nullptr)
        return false;

    const auto refreshAfterReplace = [this, main]
    {
        main->documentWasReplaced();
        updateWindowTitle();
        commandManager.commandStatusChanged();
    };

    switch (info.commandID)
    {
        case CommandIDs::fileNew:
            document->saveIfNeededAndUserAgreesAsync (
                [document, refreshAfterReplace] (juce::FileBasedDocument::SaveResult result)
                {
                    if (result != juce::FileBasedDocument::savedOk)
                        return;

                    document->setState (ProjectFactory::createDefault(), true);
                    document->setFile ({});
                    refreshAfterReplace();
                });
            return true;

        case CommandIDs::fileOpen:
            document->saveIfNeededAndUserAgreesAsync (
                [document, main, refreshAfterReplace] (juce::FileBasedDocument::SaveResult result)
                {
                    if (result != juce::FileBasedDocument::savedOk)
                        return;

                    document->loadFromUserSpecifiedFileAsync (true,
                        [document, main, refreshAfterReplace] (juce::Result loadResult)
                        {
                            if (loadResult.failed())
                                return;

                            refreshAfterReplace();
                            main->showLoadWarnings (document->getLastLoadWarnings());
                        });
                });
            return true;

        case CommandIDs::fileSave:
            document->saveAsync (true, true, [this] (juce::FileBasedDocument::SaveResult)
            {
                updateWindowTitle();
            });
            return true;

        case CommandIDs::fileSaveAs:
            document->saveAsInteractiveAsync (true, [this] (juce::FileBasedDocument::SaveResult)
            {
                updateWindowTitle();
            });
            return true;

        case CommandIDs::editUndo:
            document->getUndoManager().undo();
            updateWindowTitle();
            commandManager.commandStatusChanged();
            return true;

        case CommandIDs::editRedo:
            document->getUndoManager().redo();
            updateWindowTitle();
            commandManager.commandStatusChanged();
            return true;

        case CommandIDs::transportPlayStop:
        {
            auto& engine = main->getEngine();

            if (engine.isPlaying())
                engine.stop();
            else
                engine.play();

            return true;
        }

        case CommandIDs::transportRewind:
            main->getEngine().rewind();
            return true;

        case CommandIDs::transportToggleMode:
        {
            auto& engine = main->getEngine();
            engine.setMode (engine.getMode() == Transport::Mode::song ? Transport::Mode::pattern
                                                                      : Transport::Mode::song);
            engine.rewind();
            return true;
        }

        case CommandIDs::addChannel:
        {
            auto& undo = document->getUndoManager();
            undo.beginNewTransaction ("Add channel");
            const auto channel = ProjectEdits::addChannel (document->getState(), {}, &undo);
            main->getEditorState().setSelectedChannelId ((int) channel[ids::id]);
            updateWindowTitle();
            return true;
        }

        case CommandIDs::addPattern:
        {
            auto& undo = document->getUndoManager();
            undo.beginNewTransaction ("Add pattern");
            const auto pattern = ProjectEdits::addPattern (document->getState(), &undo);
            main->getEditorState().setCurrentPatternId ((int) pattern[ids::id]);
            main->getEngine().setCurrentPatternId ((int) pattern[ids::id]);
            updateWindowTitle();
            return true;
        }

        default:
            break;
    }

    return false;
}

// --- menu bar ----------------------------------------------------------------

juce::StringArray DewApplication::getMenuBarNames()
{
    return { "File", "Edit", "Transport", "Project" };
}

juce::PopupMenu DewApplication::getMenuForIndex (int index, const juce::String&)
{
    juce::PopupMenu menu;

    switch (index)
    {
        case 0:
            menu.addCommandItem (&commandManager, CommandIDs::fileNew);
            menu.addCommandItem (&commandManager, CommandIDs::fileOpen);
            menu.addSeparator();
            menu.addCommandItem (&commandManager, CommandIDs::fileSave);
            menu.addCommandItem (&commandManager, CommandIDs::fileSaveAs);
            break;

        case 1:
            menu.addCommandItem (&commandManager, CommandIDs::editUndo);
            menu.addCommandItem (&commandManager, CommandIDs::editRedo);
            break;

        case 2:
            menu.addCommandItem (&commandManager, CommandIDs::transportPlayStop);
            menu.addCommandItem (&commandManager, CommandIDs::transportRewind);
            menu.addSeparator();
            menu.addCommandItem (&commandManager, CommandIDs::transportToggleMode);
            break;

        case 3:
            menu.addCommandItem (&commandManager, CommandIDs::addChannel);
            menu.addCommandItem (&commandManager, CommandIDs::addPattern);
            break;

        default:
            break;
    }

    return menu;
}

} // namespace dew
