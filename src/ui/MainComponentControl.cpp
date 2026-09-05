#include "model/ProjectFactory.h"
#include "ui/DewDialog.h"
#include "ui/MainComponent.h"
#include "ui/McpConnectionsPanel.h"
#include "ui/McpConsentPanel.h"
#include "ui/PreferencesPanel.h"

namespace dew
{

// --- the document, as dew_control sees it ------------------------------------

juce::ValueTree MainComponent::ControlAdapter::project()
{
    return owner.getDocument().getState();
}

juce::UndoManager* MainComponent::ControlAdapter::undoManager()
{
    return &owner.getDocument().getUndoManager();
}

void MainComponent::ControlAdapter::flushEngine()
{
    owner.flushPendingEngineUpdate();
}

AudioEngine* MainComponent::ControlAdapter::engine()
{
    return &owner.getEngine();
}

RenderJob* MainComponent::ControlAdapter::renderJob()
{
    // Built on first use, the way the render dialog builds it: a thread that
    // exists because the application started is a thread nothing asked for.
    if (owner.renderJob == nullptr)
        owner.renderJob = std::make_unique<RenderJob>();

    return owner.renderJob.get();
}

SamplePool* MainComponent::ControlAdapter::samplePool()
{
    return &owner.samplePool;
}

SoundFontPool* MainComponent::ControlAdapter::soundFontPool()
{
    return &owner.soundFontPool;
}

bool MainComponent::ControlAdapter::newProject()
{
    // Refused while there is work nobody has saved. The user approved a client
    // to change their project, not to throw it away - and the dialog that would
    // ask them is asynchronous, so there is no answer to wait for here. A
    // client that means it can save first.
    if (owner.getDocument().hasChangedSinceSaved())
        return false;

    owner.getDocument().setState (ProjectFactory::createDefault (activeLocale()), true);

    return true;
}

bool MainComponent::ControlAdapter::openProject (const juce::File& file)
{
    if (owner.getDocument().hasChangedSinceSaved())
        return false;

    return owner.getDocument().loadFrom (file, false).wasOk();
}

namespace
{

/** True if the save finished, and finished well.

    FileBasedDocument's synchronous save() and saveAs() are compiled out here -
    they can open a file chooser, and JUCE_MODAL_LOOPS_PERMITTED is 0 in dew.
    The async forms are what remain, and with nothing to ask about
    (`askUserForFileIfNotSpecified` and `showMessageOnFailure` both false) they
    answer inline.

    So the flag is read after the call rather than trusted: if a future JUCE
    ever defers that callback, this reports a save it cannot vouch for as a
    failure rather than telling a client the file is on disk when it may not be.
*/
bool completedInline (
    const std::function<void (std::function<void (juce::FileBasedDocument::SaveResult)>)>& begin)
{
    auto answered = false;
    auto result = juce::FileBasedDocument::failedToWriteToFile;

    begin (
        [&answered, &result] (juce::FileBasedDocument::SaveResult r)
        {
            answered = true;
            result = r;
        });

    return answered && result == juce::FileBasedDocument::savedOk;
}

} // namespace

bool MainComponent::ControlAdapter::saveProject()
{
    // Only to a file the project already has. Choosing one is a decision with a
    // dialog behind it, and saveProjectAs is where a client says the path.
    if (owner.getDocument().getFile() == juce::File())
        return false;

    auto& doc = owner.getDocument();

    return completedInline ([&doc] (auto callback)
                            { doc.saveAsync (false, false, std::move (callback)); });
}

bool MainComponent::ControlAdapter::saveProjectAs (const juce::File& file)
{
    auto& doc = owner.getDocument();

    // Nothing warns about overwriting and nothing asks: this is a socket
    // talking, and a modal file chooser nobody opened is the worst thing a
    // background request could put on somebody's screen. A client that wants
    // to know whether a file is already there can look before it asks.
    return completedInline ([&doc, &file] (auto callback)
                            { doc.saveAsAsync (file, false, false, false, std::move (callback)); });
}

juce::File MainComponent::ControlAdapter::projectFile() const
{
    return owner.getDocument().getFile();
}

bool MainComponent::ControlAdapter::isProjectModified() const
{
    return owner.getDocument().hasChangedSinceSaved();
}

// --- asking the person at the keyboard ---------------------------------------

void MainComponent::ConsentAdapter::ask (const control::McpServer::ClientInfo& client,
                                         std::function<void (control::Grant)> reply)
{
    if (! hook)
    {
        // No way to ask is a refusal, not a hang. The server is waiting on this
        // callback and would otherwise sit out its whole consent timeout.
        reply (control::Grant::none);
        return;
    }

    hook ({ client.name, client.version }, std::move (reply));
}

// --- the endpoint -------------------------------------------------------------

McpGrants& MainComponent::ensureMcpGrants (Settings& settings)
{
    if (mcpGrants == nullptr)
        mcpGrants = std::make_unique<McpGrants> (settings);

    return *mcpGrants;
}

void MainComponent::applyMcpSettings (Settings& settings)
{
    mcpSettings = &settings;

    if (! settings.getMcpEnabled())
    {
        // The grants stay. They are a view over Settings, they cost nothing,
        // and the settings panel has to be able to list and revoke what was
        // allowed while the endpoint is OFF - which is exactly when somebody is
        // most likely to be looking at it.
        mcpServer.reset();
        return;
    }

    if (mcpServer == nullptr)
    {
        consentPrompt.hook = consentWithPanel (this);

        // ensureMcpGrants, never a fresh one. This function is what the switch
        // in the settings panel calls, and that panel is holding the grants by
        // pointer while it waits for the call to come back - so rebuilding them
        // here freed the object the panel read the moment control returned to
        // it. The server below holds them by reference and would go the same
        // way. They are a view over Settings; there is nothing to rebuild.
        mcpServer = std::make_unique<control::McpServer> (controlHost, ensureMcpGrants (settings),
                                                          consentPrompt);
    }

    if (mcpServer->isRunning())
        return;

    // The port the user asked for, then dew's own, then any free one. Two
    // copies of dew running is not an error, and a second one that refused to
    // listen would look like a broken feature rather than a taken port. The
    // settings panel reads the port back off the socket, so whichever it lands
    // on is the one it tells the user to connect to.
    //
    // A stored 0 means "dew's default", NOT "any free port": binding 0 first
    // would take an arbitrary port on every launch, and the whole reason there
    // is a fixed default is that `claude mcp add` should need running once.
    const auto wanted = settings.getMcpPort();

    if (wanted != 0 && mcpServer->start (wanted))
        return;

    if (mcpServer->start (control::McpServer::defaultPort))
        return;

    mcpServer->start (0);
}

void MainComponent::showMcpSettings()
{
    // The grants outlive the server being stopped, so the panel can still list
    // and revoke what was allowed while the endpoint is switched off - which is
    // exactly when somebody is most likely to be looking.
    if (mcpSettings != nullptr)
        ensureMcpGrants (*mcpSettings);

    // Both fetched on demand rather than handed over: the switch on this very
    // panel stops and starts the endpoint, and asking each time is what makes
    // the panel independent of when that happens.
    auto* panel = new McpConnectionsPanel ([this] { return mcpServer.get(); },
                                           [this] { return mcpGrants.get(); }, mcpSettings,
                                           [this]
                                           {
                                               if (mcpSettings != nullptr)
                                                   applyMcpSettings (*mcpSettings);
                                           });

    dialog::launch (panel, tr (StringId::mcp_connections_title), this);
}

void MainComponent::showPreferences (Settings& settings, juce::ApplicationCommandManager& commands,
                                     std::function<void (int)> onLanguageChosen)
{
    // The grants outlive the endpoint being stopped, exactly as showMcpSettings
    // needs them to, so they are made the same way and for the same reason.
    ensureMcpGrants (settings);

    PreferencesPanel::Hosts hosts;

    hosts.audio = &audioHost;
    hosts.engine = &engine;
    hosts.midi = &midiHost;
    hosts.commands = &commands;
    hosts.onLanguageChosen = std::move (onLanguageChosen);

    // Both fetched on demand rather than handed over: the switch on the
    // Connections page stops and starts the endpoint.
    hosts.grants = [this] { return mcpGrants.get(); };
    hosts.mcpServer = [this] { return mcpServer.get(); };
    hosts.onMcpEnabledChanged = [this]
    {
        if (mcpSettings != nullptr)
            applyMcpSettings (*mcpSettings);
    };

    PreferencesPanel::show (settings, std::move (hosts), this);
}

} // namespace dew
