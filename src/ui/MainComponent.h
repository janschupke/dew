#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "../engine/AudioEngine.h"
#include "../engine/LiveAudioHost.h"
#include "../model/ProjectDocument.h"
#include "ChannelRackComponent.h"
#include "DewLookAndFeel.h"
#include "EditorState.h"
#include "EditorTabs.h"
#include "InstrumentPanel.h"
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
                      private juce::AsyncUpdater
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

    /** Applies any pending snapshot rebuild immediately instead of waiting for
        the message loop. Edits are coalesced through an AsyncUpdater, so
        anything that needs the engine to reflect the document right now - a
        test, or a render started straight after an edit - has to ask.
    */
    void flushPendingEngineUpdate();

private:
    void handleAsyncUpdate() override;
    void projectChanged();

    DewLookAndFeel lookAndFeel;

    /** Every setTooltip call in the app was dead text until this existed:
        tooltips are drawn by a window, and there was not one anywhere.
    */
    juce::TooltipWindow tooltips { nullptr, 600 };

    ProjectDocument document;
    AudioEngine engine;
    LiveAudioHost audioHost;
    EditorState editorState;

    TransportBar transportBar;
    EditorTabs tabs;
    InstrumentPanel instrumentPanel;
    StatusBar statusBar;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace dew
