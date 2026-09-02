#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "../engine/AudioEngine.h"
#include "../engine/LiveAudioHost.h"
#include "../engine/MidiInputHost.h"
#include "../model/ProjectDocument.h"
#include "ChannelRackComponent.h"
#include "DewLookAndFeel.h"
#include "EditorState.h"
#include "EditorTabs.h"
#include "InstrumentPanel.h"
#include "../app/Settings.h"
#include "../engine/RenderJob.h"
#include "RenderPanel.h"
#include "StatusBar.h"
#include "TransportBar.h"

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

    DewLookAndFeel lookAndFeel;

    /** Every setTooltip call in the app was dead text until this existed:
        tooltips are drawn by a window, and there was not one anywhere.
    */
    juce::TooltipWindow tooltips { nullptr, 600 };

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
        explicit PanelDivider (MainComponent& o) : owner (o)
        {
            setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        }

        void mouseDown (const juce::MouseEvent&) override { widthAtDragStart = owner.panelWidth; }

        void mouseDrag (const juce::MouseEvent& event) override
        {
            owner.setPanelWidth (widthAtDragStart - event.getDistanceFromDragStartX());
        }

        void paint (juce::Graphics&) override;

    private:
        MainComponent& owner;
        int widthAtDragStart = 0;
    };

    void setPanelWidth (int);

    PanelDivider divider { *this };
    int panelWidth = Settings::defaultPanelWidth;

    static constexpr int dividerWidth = 5;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace dew
