#include "DewApplication.h"

#include "i18n/Strings.h"
#include "model/BuildInfo.h"
#include "model/DemoLibrary.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "ui/Hotkeys.h"
#include "ui/design/Animator.h"
#include "ui/design/SystemMotionPreference.h"
#include "ui/design/Theme.h"

namespace dew
{

class DewApplication::MainWindow : public juce::DocumentWindow
{
public:
    MainWindow (const juce::String& name, juce::ApplicationCommandManager& manager,
                const Settings& savedSession)
        : DocumentWindow (name, tokens::colour::background, DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new MainComponent (true), true);
        setResizable (true, false);
        setResizeLimits (900, 560, 20000, 20000);

        // Restored before the window is shown, so it does not appear centred
        // and then jump. A state that is no longer usable - a monitor that has
        // gone away - falls back to centring rather than opening offscreen.
        if (const auto state = savedSession.getWindowState(); state.isNotEmpty())
            restoreWindowStateFromString (state);
        else
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

const juce::String DewApplication::getApplicationName()
{
    return "dew";
}
const juce::String DewApplication::getApplicationVersion()
{
    return BuildInfo::version();
}

void DewApplication::initialise (const juce::String&)
{
    settings = std::make_unique<Settings>();

    // FIRST, and before anything reads a string. tr() hands out references into
    // a table setLocale rebuilds, so a label built before this call would be
    // holding a reference into a table that no longer exists - which is also
    // why the language menu says the choice takes effect next launch rather
    // than switching under a live interface.
    //
    // An empty stored tag means "follow the system", and an unresolvable one
    // negotiates down to the reference locale rather than to a blank catalogue.
    const auto language = settings->getLanguage();
    setLocale (language.isNotEmpty() ? language : juce::SystemStats::getUserLanguage());

    // Before the window exists, so it is built at the size it will be seen at
    // rather than laid out once and rescaled.
    applyUiScale (settings->getUiScale());

    // The one place motion is turned on. Everywhere else - every test, every
    // dew_shot render - leaves it off, so a widget built outside a running
    // application snaps exactly as it did before there was an animator.
    Animator::shared().setEnabled (true);
    Animator::shared().setReduceMotion (settings->getReduceMotion (systemPrefersReducedMotion()));

    // Before the window is built, so everything that copies a colour when it is
    // constructed copies the right one and there is nothing to correct on the
    // first frame.
    theme::applyPalette (theme::kindFor (settings->getThemeName()));

    mainWindow = std::make_unique<MainWindow> (getApplicationName(), commandManager, *settings);
    restoreSession();
    startTimer (autosaveIntervalMs);

    commandManager.registerAllCommandsForTarget (this);
    commandManager.setFirstCommandTarget (this);

    // A menu bar lives somewhere different on each platform and JUCE offers no
    // one call for it. macOS has a single menu bar that belongs to the
    // APPLICATION, and setMacMainMenu exists nowhere else - it is not merely a
    // no-op off Apple, it is not declared, which is why this file was the
    // second thing to stop the Linux build compiling. Everywhere else the menu
    // belongs to the WINDOW, and DocumentWindow::setMenuBar puts a
    // MenuBarComponent above the content and takes the height out of it.
    //
    // Neither call takes ownership of the model, and the model is this
    // application object, so shutdown clears it before the window goes.
#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (this);
#else
    mainWindow->setMenuBar (this);
#endif

    updateWindowTitle();
}

void DewApplication::timerCallback()
{
    saveSession();
}

void DewApplication::shutdown()
{
    stopTimer();
    saveSession();

#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (nullptr);
#else
    if (mainWindow != nullptr)
        mainWindow->setMenuBar (nullptr);
#endif

    mainWindow.reset();
    settings.reset();
}

void DewApplication::restoreSession()
{
    auto* main = getMainComponent();

    if (main == nullptr || settings == nullptr)
        return;

    main->applySettings (*settings);

    // The device before anything is heard, so the first sound already comes out
    // of whatever was chosen last time.
    if (auto state = settings->getAudioState())
        main->getAudioHost().restoreState (*state);

    // The endpoint LAST, and by reference rather than by value: a grant the
    // user gives has to be written back when they give it, which is minutes
    // after this runs. It starts nothing unless the switch is on.
    main->applyMcpSettings (*settings);
}

void DewApplication::saveSession()
{
    auto* main = getMainComponent();

    if (main == nullptr || settings == nullptr)
        return;

    main->captureSettings (*settings);

    if (mainWindow != nullptr)
        settings->setWindowState (mainWindow->getWindowStateAsString());

    if (const auto state = main->getAudioHost().getDeviceManager().createStateXml())
        settings->setAudioState (state.get());

    settings->flush();
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

    // Appended, never concatenated onto: juce::String's const char* CONSTRUCTOR
    // reads its bytes as ASCII while operator+= reads them as UTF-8, so
    // `"dew — " + name` builds the em dash through the wrong one and puts three
    // mojibake characters in the title bar. `String + const char*` is the safe
    // direction, which is why the bullet below was right and the dash was not.
    // The gate "no source starts a concatenation with a non-ASCII literal" is
    // what keeps this from coming back.
    juce::String title ("dew");
    title += " — ";
    title += name;

    if (document->hasChangedSinceSaved())
        title += " •";

    mainWindow->setName (title);
}

void DewApplication::systemRequestedQuit()
{
    // Saved here as well as in shutdown, because this is the point at which the
    // window still exists and its geometry can still be read. It is also the
    // only path a user quit actually takes.
    saveSession();

    auto* document = getDocument();

    if (document == nullptr)
    {
        quit();
        return;
    }

    // Async: a modal loop here would block the message thread the audio device
    // callbacks and the UI both depend on.
    // JUCEApplication::quit() is static, so this lambda captures nothing.
    document->saveIfNeededAndUserAgreesAsync (
        [] (juce::FileBasedDocument::SaveResult result)
        {
            if (result == juce::FileBasedDocument::savedOk)
                quit();
        });
}

// --- commands ----------------------------------------------------------------

void DewApplication::getAllCommands (juce::Array<juce::CommandID>& commands)
{
    for (const auto& binding : hotkeys::application())
        commands.add (binding.action);
}

void DewApplication::getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    if (! hotkeys::describe (id, info))
        return;

    // What is left is the only part of a command that is not a constant: what
    // it needs in order to be available at all. Everything else - the name, the
    // menu it sits under, the key that reaches it - is a row in the registry,
    // and used to be ninety lines of switch here that no test could see.
    auto* document = getDocument();
    auto* main = getMainComponent();

    switch (id)
    {
        case CommandIDs::editUndo:
            info.setActive (document != nullptr && document->getUndoManager().canUndo());
            break;

        case CommandIDs::editRedo:
            info.setActive (document != nullptr && document->getUndoManager().canRedo());
            break;

        case CommandIDs::fileRender: info.setActive (document != nullptr && main != nullptr); break;

        case CommandIDs::fileSave:
        case CommandIDs::fileSaveAs:
        case CommandIDs::addChannel:
        case CommandIDs::addPattern: info.setActive (document != nullptr); break;

        case CommandIDs::fileNew:
        case CommandIDs::fileOpen: break;

        case CommandIDs::viewUiScaleFirst:
        case CommandIDs::viewUiScale125:
        case CommandIDs::viewUiScale150:
        case CommandIDs::viewUiScale175:
        {
            // Ticked rather than merely listed: four items that all read as
            // available say nothing about which one you are looking at.
            const auto step = (int) (id - CommandIDs::viewUiScaleFirst);

            info.setActive (settings != nullptr);
            info.setTicked (
                settings != nullptr && step >= 0 && step < Settings::numUiScaleSteps
                && juce::approximatelyEqual (settings->getUiScale(), Settings::uiScaleSteps[step]));
            break;
        }

        case CommandIDs::viewMotionFirst:
        case CommandIDs::viewMotionFull:
        case CommandIDs::viewMotionReduced:
        {
            const auto step = (int) (id - CommandIDs::viewMotionFirst);

            info.setActive (settings != nullptr);
            info.setTicked (settings != nullptr && (int) settings->getMotionPreference() == step);
            break;
        }

        case CommandIDs::viewThemeFirst:
        case CommandIDs::viewThemeHighContrast:
        {
            const auto step = (int) (id - CommandIDs::viewThemeFirst);

            info.setActive (settings != nullptr);
            info.setTicked ((int) theme::current() == step);
            break;
        }

        default:
            // Everything else needs the editor, and nothing more.
            info.setActive (main != nullptr);
            break;
    }
}

void DewApplication::applyUiScale (double scale)
{
    juce::Desktop::getInstance().setGlobalScaleFactor ((float) scale);

    // setGlobalScaleFactor refreshes the displays, which is what tells the peer
    // its transform changed - but the content component is laid out in its own
    // coordinates and has no reason to know. Ask it directly, or a scale change
    // shows at the next resize and not before.
    if (auto* main = getMainComponent())
        main->resized();
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

                    document->setState (ProjectFactory::createDefault (activeLocale()), true);
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

                    document->loadFromUserSpecifiedFileAsync (
                        true,
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
                                 { updateWindowTitle(); });
            return true;

        case CommandIDs::fileSaveAs:
            document->saveAsInteractiveAsync (true, [this] (juce::FileBasedDocument::SaveResult)
                                              { updateWindowTitle(); });
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

        case CommandIDs::transportRewind: main->getEngine().rewind(); return true;

        case CommandIDs::transportToggleMode:
        {
            auto& engine = main->getEngine();
            engine.setMode (engine.getMode() == Transport::Mode::song ? Transport::Mode::pattern
                                                                      : Transport::Mode::song);
            engine.rewind();
            return true;
        }

        case CommandIDs::transportRecord:
            if (const auto error = main->toggleRecording(); error.isNotEmpty())
                main->showLoadWarnings ({ error });

            return true;

        case CommandIDs::addChannel:
        {
            auto& undo = document->getUndoManager();
            undo.beginNewTransaction ("Add channel");
            const auto channel = ProjectEdits::addChannel (document->getState(), {}, &undo);
            main->getEditorState().setSelectedChannelId ((int) channel[ids::id]);
            updateWindowTitle();
            return true;
        }

        case CommandIDs::fileRender: main->showRenderDialog (settings.get()); return true;

        case CommandIDs::audioSettings: main->showAudioSettings(); return true;

        case CommandIDs::midiSettings: main->showMidiSettings(); return true;
        case CommandIDs::mcpSettings: main->showMcpSettings(); return true;

        case CommandIDs::compileScore: main->compileScore(); return true;

        // The editors, by number. These reach here at all only because the
        // timeline map now compares modifiers: it used to match a bare digit
        // and swallow cmd-1 in whichever view had focus.
        case CommandIDs::viewChannelRack: main->showTab (0); return true;
        case CommandIDs::viewPianoRoll: main->showTab (1); return true;
        case CommandIDs::viewPlaylist: main->showTab (2); return true;
        case CommandIDs::viewMixer: main->showTab (3); return true;
        case CommandIDs::viewScore: main->showTab (4); return true;

        case CommandIDs::viewNextTab: main->showAdjacentTab (1); return true;
        case CommandIDs::viewPreviousTab: main->showAdjacentTab (-1); return true;

        case CommandIDs::viewToggleInstrumentPanel: main->toggleInstrumentPanel(); return true;

        case CommandIDs::viewUiScaleFirst:
        case CommandIDs::viewUiScale125:
        case CommandIDs::viewUiScale150:
        case CommandIDs::viewUiScale175:
        {
            const auto step = (int) (info.commandID - CommandIDs::viewUiScaleFirst);

            if (settings == nullptr || step < 0 || step >= Settings::numUiScaleSteps)
                return false;

            settings->setUiScale (Settings::uiScaleSteps[step]);
            applyUiScale (Settings::uiScaleSteps[step]);
            commandManager.commandStatusChanged();
            return true;
        }

        case CommandIDs::viewMotionFirst:
        case CommandIDs::viewMotionFull:
        case CommandIDs::viewMotionReduced:
        {
            const auto step = (int) (info.commandID - CommandIDs::viewMotionFirst);

            if (settings == nullptr || step < 0 || step > (int) Settings::Motion::reduced)
                return false;

            settings->setMotionPreference ((Settings::Motion) step);
            Animator::shared().setReduceMotion (
                settings->getReduceMotion (systemPrefersReducedMotion()));
            commandManager.commandStatusChanged();
            return true;
        }

        case CommandIDs::viewThemeFirst:
        case CommandIDs::viewThemeHighContrast:
        {
            const auto kind = info.commandID == CommandIDs::viewThemeHighContrast
                                  ? theme::Kind::highContrast
                                  : theme::Kind::dark;

            if (settings != nullptr)
                settings->setThemeName (theme::name (kind));

            if (main != nullptr)
                theme::apply (kind, *main);

            commandManager.commandStatusChanged();
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

        default: break;
    }

    return false;
}

} // namespace dew
