#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "engine/AudioEngine.h"
#include "io/LiveAudioHost.h"
#include "io/MidiInputHost.h"
#include "io/SamplePool.h"
#include "io/SoundFontPool.h"
#include "app/ProjectDocument.h"
#include "ui/ChannelRackComponent.h"
#include "ui/design/DewLookAndFeel.h"
#include "control/McpServer.h"
#include "ui/EditorState.h"
#include "ui/McpConsentPanel.h"
#include "ui/McpGrants.h"
#include "ui/EditorTabs.h"
#include "ui/InstrumentPanel.h"
#include "ui/ParamContextMenu.h"
#include "app/Settings.h"
#include "io/RenderJob.h"
#include "ui/RenderPanel.h"
#include "ui/HoverHelp.h"
#include "ui/StatusBar.h"
#include "ui/TransportBar.h"
#include "ui/design/Animator.h"

namespace dew
{

/** The editor: a transport bar, the four editors, and the instrument panel.

    Owns the document and the engine, and is the one place that knows the two
    are connected - every project change rebuilds the engine's snapshot, and
    the rebuild is coalesced through an AsyncUpdater so that dragging a knob
    causes one rebuild per message-loop turn rather than one per pixel.
*/
class MainComponent : public juce::Component,
                      private juce::AsyncUpdater,
                      private juce::ChangeListener
{
public:
    /** Folds the instrument panel away, and unfolds it. Public because the
        fold is animated, and there is nothing in a rendered image to measure a
        LAYOUT by - so a test drives it and reads the width back. */
    void setPanelCollapsedForTesting (bool collapsed)
    {
        setPanelCollapsed (collapsed);
    }

    /** The panel's width as laid out, which during a fold is neither the open
        width nor zero. */
    int getInstrumentPanelWidthForTesting() const;

    /** @param openAudioDevice  false for headless use - screenshots and CI have
                                  no reason to take over the sound card.
    */
    explicit MainComponent (bool openAudioDevice);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    ProjectDocument& getDocument() noexcept
    {
        return document;
    }
    AudioEngine& getEngine() noexcept
    {
        return engine;
    }
    EditorState& getEditorState() noexcept
    {
        return editorState;
    }

    /** Shows the score tab and compiles what is in it into the project. */
    void compileScore()
    {
        tabs.compileScore();
    }

    // --- navigation ----------------------------------------------------------
    /** Brings one editor to the front, by index, clamped to the tabs there are.

        The tabs are a private member because nothing outside had any business
        reaching into them; the keyboard now has, so this is the seam rather
        than an accessor that would hand out the whole TabbedComponent.
    */
    void showTab (int index);

    /** The next editor along, wrapping. Negative goes back. */
    void showAdjacentTab (int delta);

    int getActiveTab() const;
    int getNumEditorTabs() const;

    /** Folds the instrument panel away, or brings it back. */
    void toggleInstrumentPanel();

    /** Rebuilds every view after the document is replaced by New or Open. */
    void documentWasReplaced();

    /** Warnings from the last load, shown once in the status line. */
    void showLoadWarnings (const juce::StringArray&);

    /** Says that the language has changed and applies next launch.

        Here because the status bar is MainComponent's, and the menu that makes
        the change lives in the application above it. */
    void showLanguageNotice();

    /** Opens the audio settings over this window. */
    void showAudioSettings();

    /** Opens the MIDI settings over this window. */
    void showMidiSettings();

    /** Opens the preferences window over this one.

        Takes the command manager because three of its settings ARE commands -
        theme, motion and interface size - and the page invokes them rather than
        applying them, so DewApplication::perform stays the only implementation.
        `onLanguageChosen` is the application's own chooseLanguage, for the one
        setting that is data rather than a command.

        This component supplies the hosts; the panel embeds the very same
        AudioSettingsPanel, MidiSettingsPanel and McpConnectionsPanel that
        showAudioSettings, showMidiSettings and showMcpSettings open, which is
        why those three menu items still work and still mean the same thing.
    */
    void showPreferences (Settings&, juce::ApplicationCommandManager&,
                          std::function<void (int)> onLanguageChosen);

    /** Opens the render dialog over this window.

        Takes the Settings the app owns, so the dialog can remember the format
        and the folder; null is allowed, and simply remembers nothing.
    */
    void showRenderDialog (Settings* settingsToUpdate = nullptr);

    /** Restores what was saved last time, and captures it again on exit. The
        app owns the store; this only knows how to read and write itself.
    */
    void applySettings (const Settings&);
    void captureSettings (Settings&) const;

    LiveAudioHost& getAudioHost() noexcept
    {
        return audioHost;
    }
    MidiInputHost& getMidiHost() noexcept
    {
        return midiHost;
    }

    // --- recording -----------------------------------------------------------
    /** Starts or stops a take on the armed channel.

        Returns an empty string, or a reason the take could not start: nothing
        armed, no audio input, or no device. The caller shows it - this returns
        the message rather than posting it so that a test can read it.
    */
    juce::String toggleRecording();

    bool isRecording() const noexcept;

    /** The audio behind the project's audio channels. Shared with the engine,
        and with every view that draws a waveform.
    */
    SamplePool& getSamplePool() noexcept
    {
        return samplePool;
    }

    /** Applies any pending snapshot rebuild immediately instead of waiting for
        the message loop. Edits are coalesced through an AsyncUpdater, so
        anything that needs the engine to reflect the document right now - a
        test, or a render started straight after an edit - has to ask.
    */
    void flushPendingEngineUpdate();

    // --- the MCP endpoint ----------------------------------------------------
    /** Starts or stops the local MCP endpoint to match what is stored.

        Takes the Settings by reference and KEEPS it, unlike applySettings which
        only reads: a grant the user gives has to be written back at the moment
        they give it, which is a message that arrives long after startup.

        Safe to call again; it starts nothing that is already running and stops
        what the switch has turned off.
    */
    void applyMcpSettings (Settings&);

    /** Opens the MCP settings over this window. */
    void showMcpSettings();

    /** The endpoint, or nullptr when it has never been started. Public so a
        test and dew_shot can ask what it is doing without a socket. */
    control::McpServer* getMcpServer() noexcept
    {
        return mcpServer.get();
    }

private:
    /** Picks the destination and starts the background render. Split from the
        dialog so that the panel never touches a file, which is what keeps it
        constructible in a test and in dew_shot.
    */
    void startRender (const RenderPanel::Request&, Settings* settingsToUpdate);

    void handleAsyncUpdate() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void projectChanged();

    /** Points MIDI input at whatever channel is selected, resolved to the index
        the engine uses. The MIDI thread must never read EditorState, so the
        index is pushed down to the router from here instead.
    */
    void updateMidiTargetChannel();

    /** Hands the editor's time selections to the engine as loop windows.

        Both are pushed on every change, one per transport mode, so neither
        depends on which mode is current - the mode is set from the transport bar
        and from a menu command, and neither of those comes through here.
    */
    void updateLoopRange();

    /** Turns a finished take into a source on the armed channel and a clip on
        the playlist, as one undo step.
    */
    void finishRecording();

    DewLookAndFeel lookAndFeel;

    /** Every setTooltip call in the app was dead text until this existed:
        tooltips are drawn by a window, and there was not one anywhere.
    */
    juce::TooltipWindow tooltips { nullptr, 600 };

    // And HoverHelp puts the same text in the status bar with no delay at all,
    // so a control answers "what is this?" while the pointer is still moving.

    /** The audio behind the project's audio channels.

        Declared FIRST, so it is destroyed last. Both the engine and a running
        render hold a bare pointer to it - the engine reads it on the message
        thread when it builds a snapshot, and RenderJob reads it on its own
        thread - and a member declared later would be destroyed while the render
        thread was still using it.
    */
    SamplePool samplePool;

    /** Soundfonts, cached the way samples are. Declared beside the sample pool
        so it outlives the render job that borrows it - see RenderJob below. */
    SoundFontPool soundFontPool;

    /** The one render running, if any. Owned here rather than by the dialog so
        that closing the dialog does not kill the render, and so its destructor
        joins the thread before anything it renders from goes away.
    */
    std::unique_ptr<RenderJob> renderJob;

    ProjectDocument document;

    AudioEngine engine;
    LiveAudioHost audioHost;

    /** Shares the audio host's device manager, which is the one object that
        knows about both kinds of device.
    */
    MidiInputHost midiHost;

    EditorState editorState;

    TransportBar transportBar;
    /** What every control's right-click menu needs and no panel knows: where
        the playhead is, and how to show the clip it makes.

        Declared BEFORE the panels that take a pointer to it, so it outlives
        them - a member's destruction order is the reverse of its declaration
        order, and a panel unhooking on the way down must not read a host that
        has already gone.
    */
    paramMenu::Host paramMenuHost;

    EditorTabs tabs;
    InstrumentPanel instrumentPanel;

    /** Scrolls the instrument panel when the window is too short to hold it.

        The panel stacks fixed-height rows and hands the effect chain whatever
        is left, so at the smallest window the app can open there was nothing
        left: the chain vanished and the last knob row was cut in half. A
        viewport is what the channel rack, the mixer, the chain host and the
        MIDI settings list already use for the same problem.

        It changes nothing when there IS room - the panel is sized to the
        greater of the viewport and its own required height, so at any ordinary
        window size it is exactly as tall as the viewport and no scrollbar
        appears.

        Declared AFTER the panel so it is destroyed BEFORE it: it holds a
        pointer it does not own.
    */
    juce::Viewport panelViewport;

    StatusBar statusBar;

    /** Puts what the pointer is over into the status bar, at once.

        After statusBar, so it is destroyed BEFORE it - it holds a reference and
        reports into it from a mouse event. Listening to `*this` reaches every
        control in the window through one object rather than a hook on each.
    */
    HoverHelp hoverHelp { *this, statusBar };

    /** Drags the boundary between the editor and the instrument panel.

        The panel was a hard-coded 300px, so there was no position to remember;
        this is what makes "panel positions" something that can be persisted.
    */
    class PanelDivider : public juce::Component
    {
    public:
        explicit PanelDivider (MainComponent& o);

        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;

        void paint (juce::Graphics&) override;
        void resized() override;

        /** Points the chevron the way the panel will go when it is pressed. */
        void updateToggle();

    private:
        MainComponent& owner;
        int widthAtDragStart = 0;

        /** True while a right press is still down, so the drag it arms moves
            nothing. See PopupPress. */
        bool popupPressed = false;

        /** At the top of the divider rather than inside the panel: it has to
            stay reachable once the panel it hides is gone.
        */
        DewIconButton toggleButton { icons::chevronRight(), "Hide the instrument panel" };
    };

    /** MainComponent as dew_control sees it.

        An adapter rather than MainComponent implementing ControlHost itself,
        and not for taste: the interface has engine(), samplePool() and
        renderJob(), and this class already has MEMBERS of all three names, so
        the direct form does not compile. Holding a reference instead also keeps
        MainComponent's public surface the editor's rather than the protocol's.
    */
    class ControlAdapter : public control::ControlHost
    {
    public:
        explicit ControlAdapter (MainComponent& o)
            : owner (o)
        {
        }

        juce::ValueTree project() override;
        juce::UndoManager* undoManager() override;
        void flushEngine() override;

        AudioEngine* engine() override;
        RenderJob* renderJob() override;
        SamplePool* samplePool() override;
        SoundFontPool* soundFontPool() override;

        bool newProject() override;
        bool openProject (const juce::File&) override;
        bool saveProject() override;
        bool saveProjectAs (const juce::File&) override;
        juce::File projectFile() const override;
        bool isProjectModified() const override;

    private:
        MainComponent& owner;
    };

    /** Raises the consent dialog when the server asks.

        The hook is the seam: JUCE_MODAL_LOOPS_PERMITTED is 0, so the dialog
        cannot be driven to an answer inline, and a test replaces this to answer
        without one ever opening.
    */
    class ConsentAdapter : public control::McpServer::ConsentPrompt
    {
    public:
        void ask (const control::McpServer::ClientInfo&,
                  std::function<void (control::Grant)> reply) override;

        ConsentHook hook;
    };

    ControlAdapter controlHost { *this };
    ConsentAdapter consentPrompt;

    /** Null until applyMcpSettings has been called, which is what makes the
        endpoint something the application turns on rather than something a
        window opens by existing. */
    std::unique_ptr<McpGrants> mcpGrants;
    std::unique_ptr<control::McpServer> mcpServer;

    /** Kept, not copied: a grant the user gives has to be written back at the
        moment they give it, and the switch in the settings panel writes here
        too. Null until applyMcpSettings has been called. */
    Settings* mcpSettings = nullptr;

    void setPanelWidth (int);
    void setPanelCollapsed (bool);

    PanelDivider divider { *this };
    int panelWidth = Settings::defaultPanelWidth;

    /** How far the instrument panel has folded away, 0 open and 1 collapsed.

        A layout, not a paint, so it drives resized() rather than a repaint -
        repainting a component does not lay it out again.
    */
    ComponentMotion collapse { *this };
    bool panelCollapsed = false;

    /** Wide enough to hold the collapse toggle. It was five pixels of drag
        handle, which is not enough room for a control.
    */
    static constexpr int dividerWidth = 16;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace dew
