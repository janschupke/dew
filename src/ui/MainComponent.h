#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "engine/AudioEngine.h"
#include "io/LiveAudioHost.h"
#include "io/MidiInputHost.h"
#include "io/SamplePool.h"
#include "model/ProjectDocument.h"
#include "ui/ChannelRackComponent.h"
#include "ui/DewLookAndFeel.h"
#include "ui/EditorState.h"
#include "ui/EditorTabs.h"
#include "ui/InstrumentPanel.h"
#include "app/Settings.h"
#include "io/RenderJob.h"
#include "ui/RenderPanel.h"
#include "ui/StatusBar.h"
#include "ui/TransportBar.h"

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
    /** @param openAudioDevice  false for headless use - screenshots and CI have
                                  no reason to take over the sound card.
    */
    explicit MainComponent (bool openAudioDevice = true);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    ProjectDocument& getDocument() noexcept  { return document; }
    AudioEngine& getEngine() noexcept        { return engine; }
    EditorState& getEditorState() noexcept   { return editorState; }

    /** Rebuilds every view after the document is replaced by New or Open. */
    void documentWasReplaced();

    /** Warnings from the last load, shown once in the status line. */
    void showLoadWarnings (const juce::StringArray&);

    /** Opens the audio settings over this window. */
    void showAudioSettings();

    /** Opens the MIDI settings over this window. */
    void showMidiSettings();

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

    LiveAudioHost& getAudioHost() noexcept { return audioHost; }
    MidiInputHost& getMidiHost() noexcept  { return midiHost; }

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
    SamplePool& getSamplePool() noexcept  { return samplePool; }

    /** Applies any pending snapshot rebuild immediately instead of waiting for
        the message loop. Edits are coalesced through an AsyncUpdater, so
        anything that needs the engine to reflect the document right now - a
        test, or a render started straight after an edit - has to ask.
    */
    void flushPendingEngineUpdate();

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

    /** The audio behind the project's audio channels.

        Declared FIRST, so it is destroyed last. Both the engine and a running
        render hold a bare pointer to it - the engine reads it on the message
        thread when it builds a snapshot, and RenderJob reads it on its own
        thread - and a member declared later would be destroyed while the render
        thread was still using it.
    */
    SamplePool samplePool;

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
    EditorTabs tabs;
    InstrumentPanel instrumentPanel;
    StatusBar statusBar;

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

        /** At the top of the divider rather than inside the panel: it has to
            stay reachable once the panel it hides is gone.
        */
        DewIconButton toggleButton { icons::chevronRight(), "Hide the instrument panel" };
    };

    void setPanelWidth (int);
    void setPanelCollapsed (bool);

    PanelDivider divider { *this };
    int panelWidth = Settings::defaultPanelWidth;
    bool panelCollapsed = false;

    /** Wide enough to hold the collapse toggle. It was five pixels of drag
        handle, which is not enough room for a control.
    */
    static constexpr int dividerWidth = 16;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace dew
