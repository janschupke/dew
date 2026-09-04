#include "ui/MainComponent.h"
#include "ui/DewDialog.h"

#include <cmath>

#include "model/AssetPaths.h"
#include "model/Meter.h"

#include "ui/AudioSettingsPanel.h"
#include "ui/MidiSettingsPanel.h"

#include "model/ProjectEdits.h"
#include "ui/design/Cursors.h"
#include "ui/design/Tokens.h"

#include "model/Ids.h"
#include "ui/design/Animator.h"
#include "ui/design/SystemMotionPreference.h"

namespace dew
{

MainComponent::MainComponent (bool openAudioDevice)
    : audioHost (engine)
    , midiHost (audioHost.getDeviceManager(), engine)
    , transportBar (document, engine, editorState)
    , tabs (document, engine, editorState, &samplePool)
    , instrumentPanel (document, editorState, &samplePool, &soundFontPool)
    , statusBar (document, editorState, audioHost)
{
    // Every spec-built control's right-click, wired once here because this is
    // the only object that knows all three halves: the document, where the
    // playhead is, and how to show what gets made.
    paramMenuHost.document = &document;

    paramMenuHost.startBar = [this]
    {
        // Where you are LISTENING, not bar zero. A curve made while a song is
        // playing should appear under the playhead rather than at the top of an
        // arrangement you would then have to go and find it in.
        const auto stepsPerBar = juce::jmax (1, Meter::of (document.getState()).stepsPerBar());

        return juce::jmax (0, (int) (engine.getPlayheadSteps() / (double) stepsPerBar));
    };

    paramMenuHost.reveal = [this] (juce::ValueTree)
    {
        // Show the arrangement. Making something the user cannot see is worse
        // than not offering to make it.
        tabs.setCurrentTabIndex (EditorTabs::playlistTabIndex);
    };

    tabs.setParamMenuHost (&paramMenuHost);
    transportBar.setParamMenuHost (&paramMenuHost);
    instrumentPanel.setParamMenuHost (&paramMenuHost);

    // A layout, not a paint.
    collapse.onChanged = [this] { resized(); };

    juce::Desktop::getInstance().setDefaultLookAndFeel (&lookAndFeel);

    // Before the first projectChanged(), or the opening snapshot would resolve
    // every audio channel to silence.
    engine.setSamplePool (&samplePool);
    engine.setSoundFontPool (&soundFontPool);

    addAndMakeVisible (transportBar);
    addAndMakeVisible (tabs);
    addAndMakeVisible (instrumentPanel);

    // Any document change schedules a snapshot rebuild. Coalescing through the
    // AsyncUpdater means a knob drag costs one rebuild per message-loop turn
    // rather than one per mouse move.
    document.onProjectChanged = [this] { triggerAsyncUpdate(); };

    // Push the initial project before opening the device, so the first block
    // the engine renders already has something in it.
    projectChanged();

    addAndMakeVisible (statusBar);
    addAndMakeVisible (divider);

    // The score editor says what a compile did, and does not otherwise reach
    // out of its tab. Errors surface in three places doing three jobs: the
    // squiggle says where, the list under the editor says what, and this says
    // whether the project was written to at all.
    tabs.getScoreEditor().onMessage =
        [this] (const juce::String& message, StatusBar::Severity severity)
    { statusBar.showMessage (message, severity); };

    transportBar.onToggleRecord = [this]
    {
        if (const auto error = toggleRecording(); error.isNotEmpty())
            statusBar.showMessage (error, StatusBar::Severity::warning);
    };

    transportBar.isRecording = [this] { return isRecording(); };

    transportBar.onMeterChanged = [this] (bool wasExact)
    {
        const auto meter = Meter::of (document.getState());

        // Said once, here, rather than left to be noticed: redefining a bar
        // moves every clip, and a ratio that does not divide evenly has to
        // round one to the nearest whole bar.
        if (wasExact)
            statusBar.showMessage ("Time signature is now " + meter.toString() + ".",
                                   StatusBar::Severity::info);
        else
            statusBar.showMessage ("Time signature is now " + meter.toString()
                                       + ". Some clips were rounded to the nearest bar.",
                                   StatusBar::Severity::warning);
    };

    // Which channel MIDI plays follows the selection, and has to be pushed to
    // the router as an index because the MIDI thread cannot read EditorState.
    editorState.addChangeListener (this);
    updateMidiTargetChannel();
    updateLoopRange();

    if (! openAudioDevice)
    {
        statusBar.showMessage ("Audio device not opened", StatusBar::Severity::warning);
    }
    else if (const auto error = audioHost.start(); error.isNotEmpty())
    {
        statusBar.showMessage ("Audio unavailable: " + error, StatusBar::Severity::error);
    }
    else
    {
        statusBar.showMessage (audioHost.describeDevice(), StatusBar::Severity::info);
    }

    // MIDI is opened alongside audio, and skipped for the same reason: a
    // screenshot or a CI run has no business taking over the ports. Anything
    // the saved state re-enabled starts working here, without the panel being
    // opened at all.
    if (openAudioDevice)
        midiHost.start();

    setSize (1180, 760);
}

MainComponent::~MainComponent()
{
    cancelPendingUpdate();
    editorState.removeChangeListener (this);
    midiHost.stop();
    audioHost.stop();
    juce::Desktop::getInstance().setDefaultLookAndFeel (nullptr);
}

void MainComponent::handleAsyncUpdate()
{
    projectChanged();
}

void MainComponent::projectChanged()
{
    // Before the snapshot: a relative audio path can only be resolved against
    // where the document lives, and Save As moves that out from under it.
    samplePool.setProjectFile (document.getFile());
    soundFontPool.setProjectFile (document.getFile());

    juce::StringArray warnings;
    engine.setProject (document.getState(), &warnings);

    // Channels may have been added or removed, which moves every index after
    // them - including the one MIDI is pointed at.
    updateMidiTargetChannel();
    updateLoopRange();

    // A project that cannot be rendered as the user expects is worth saying so
    // once rather than silently playing something else. It expires on its own
    // now instead of standing until something overwrites it.
    if (! warnings.isEmpty())
        statusBar.showMessage (warnings[0], StatusBar::Severity::warning);
}

void MainComponent::documentWasReplaced()
{
    document.onProjectChanged = [this] { triggerAsyncUpdate(); };

    transportBar.refresh();
    tabs.refresh();
    instrumentPanel.refresh();
    statusBar.refresh();

    engine.stop();
    engine.rewind();
    projectChanged();
}

void MainComponent::flushPendingEngineUpdate()
{
    handleUpdateNowIfNeeded();
}

void MainComponent::showLoadWarnings (const juce::StringArray& warnings)
{
    if (warnings.isEmpty())
        return;

    statusBar.showMessage (juce::String (warnings.size()) + " item"
                               + (warnings.size() == 1 ? "" : "s")
                               + " in this file were not understood: " + warnings[0],
                           StatusBar::Severity::warning);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (tokens::colour::background);
}

MainComponent::PanelDivider::PanelDivider (MainComponent& o)
    : owner (o)
{
    setMouseCursor (cursor::resizeX);

    setComponentID ("panelDivider");

    toggleButton.setComponentID ("panelToggle");
    toggleButton.setMouseClickGrabsKeyboardFocus (false);
    toggleButton.setMouseCursor (cursor::clickable);
    toggleButton.onClick = [this] { owner.setPanelCollapsed (! owner.panelCollapsed); };
    addAndMakeVisible (toggleButton);
}

void MainComponent::PanelDivider::mouseDown (const juce::MouseEvent&)
{
    widthAtDragStart = owner.panelWidth;
}

void MainComponent::PanelDivider::mouseDrag (const juce::MouseEvent& event)
{
    // Dragging brings a folded panel back. Otherwise a collapse would strand
    // the width being dragged behind a panel nothing can be seen of.
    owner.setPanelCollapsed (false);
    owner.setPanelWidth (widthAtDragStart - event.getDistanceFromDragStartX());
}

void MainComponent::PanelDivider::updateToggle()
{
    toggleButton.setIcon (owner.panelCollapsed ? icons::chevronLeft() : icons::chevronRight());
    toggleButton.setTooltip (owner.panelCollapsed ? "Show the instrument panel"
                                                  : "Hide the instrument panel");
}

void MainComponent::PanelDivider::resized()
{
    toggleButton.setBounds (
        getLocalBounds().removeFromTop (tokens::size::iconButton).reduced (0, tokens::space::xxs));
}

void MainComponent::PanelDivider::paint (juce::Graphics& g)
{
    g.fillAll (tokens::colour::background);

    // Below the toggle only: a rule drawn through the button reads as a line
    // with a hole in it.
    g.setColour (isMouseOverOrDragging() ? tokens::colour::accent : tokens::colour::dividerStrong);
    g.drawVerticalLine (getWidth() / 2, (float) toggleButton.getBottom(), (float) getHeight());
}

void MainComponent::setPanelWidth (int width)
{
    const auto clamped = juce::jlimit (Settings::minPanelWidth, Settings::maxPanelWidth, width);

    if (clamped == panelWidth)
        return;

    panelWidth = clamped;
    resized();
}

int MainComponent::getInstrumentPanelWidthForTesting() const
{
    return instrumentPanel.getWidth();
}

void MainComponent::setPanelCollapsed (bool collapsed)
{
    if (collapsed == panelCollapsed)
        return;

    panelCollapsed = collapsed;
    divider.updateToggle();
    collapse.animateTo (collapsed ? 1.0f : 0.0f, tokens::motion::panelMs);
    resized();
}

void MainComponent::showTab (int index)
{
    // `true` so the arrival fade runs: a tab reached from the keyboard has to
    // look like a tab reached from the bar, and sendChangeMessage is what
    // drives EditorTabs::currentTabChanged.
    tabs.setCurrentTabIndex (juce::jlimit (0, juce::jmax (0, tabs.getNumTabs() - 1), index), true);
}

void MainComponent::showAdjacentTab (int delta)
{
    const auto count = tabs.getNumTabs();

    if (count <= 0)
        return;

    // Wraps, because a cycle that stops at the ends is a cycle you have to
    // look at to use.
    const auto next = ((tabs.getCurrentTabIndex() + delta) % count + count) % count;

    showTab (next);
}

int MainComponent::getActiveTab() const
{
    return tabs.getCurrentTabIndex();
}
int MainComponent::getNumEditorTabs() const
{
    return tabs.getNumTabs();
}

void MainComponent::toggleInstrumentPanel()
{
    setPanelCollapsed (! panelCollapsed);
}

void MainComponent::applySettings (const Settings& settings)
{
    panelWidth = settings.getPanelWidth();
    panelCollapsed = settings.getPanelCollapsed();
    divider.updateToggle();

    Animator::shared().setReduceMotion (settings.getReduceMotion (systemPrefersReducedMotion()));

    editorState.setSelectedChannelId (settings.getSelectedChannelId());
    editorState.setSelectedMixerTrackId (settings.getSelectedMixerTrackId());
    editorState.setCurrentPatternId (settings.getCurrentPatternId());

    tabs.setCurrentTabIndex (settings.getTabIndex(), false);
    tabs.applyPianoRollView (settings.getPianoRollZoom(), settings.getPianoRollScroll(),
                             settings.getPianoRollPitchScroll());
    tabs.setPianoRollSnap (settings.getPianoRollSnap());
    tabs.setPlaylistTrackHeight (settings.getPlaylistTrackHeight());
    tabs.setPianoRollRowHeight (settings.getPianoRollRowHeight());
    tabs.setScoreFontStep (settings.getScoreFontStep());

    // Which devices are enabled rides the audio device XML, restored by the
    // app; only these two are dew's own.
    midiHost.getRouter().setChannelFilter (settings.getMidiChannelFilter());
    midiHost.getRouter().setTranspose (settings.getMidiTranspose());

    resized();
}

void MainComponent::captureSettings (Settings& settings) const
{
    settings.setPanelWidth (panelWidth);
    settings.setPanelCollapsed (panelCollapsed);
    settings.setTabIndex (tabs.getCurrentTabIndex());
    settings.setSelectedChannelId (editorState.getSelectedChannelId());
    settings.setSelectedMixerTrackId (editorState.getSelectedMixerTrackId());
    settings.setCurrentPatternId (editorState.getCurrentPatternId());

    double zoom = 0.0, scroll = 0.0, pitch = 0.0;
    tabs.capturePianoRollView (zoom, scroll, pitch);

    settings.setMidiChannelFilter (midiHost.getRouter().getChannelFilter());
    settings.setMidiTranspose (midiHost.getRouter().getTranspose());

    settings.setPianoRollZoom (zoom);
    settings.setPianoRollScroll (scroll);
    settings.setPianoRollPitchScroll (pitch);
    settings.setPianoRollSnap (tabs.getPianoRollSnap());
    settings.setPlaylistTrackHeight (tabs.getPlaylistTrackHeight());
    settings.setPianoRollRowHeight (tabs.getPianoRollRowHeight());
    settings.setScoreFontStep (tabs.getScoreFontStep());
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateMidiTargetChannel();
    updateLoopRange();
}

void MainComponent::updateLoopRange()
{
    // The piano roll selects steps of a pattern; the playlist selects bars of
    // the song. Both are already in the units their editor works in, so the only
    // conversion is the playlist's bars into steps.
    const auto steps = editorState.getSelectedStepRange();

    if (steps.isEmpty())
        engine.clearLoopRange (Transport::Mode::pattern);
    else
        engine.setLoopRangeSteps (Transport::Mode::pattern, (double) steps.getStart(),
                                  (double) steps.getEnd());

    const auto bars = editorState.getSelectedBarRange();

    if (bars.isEmpty())
    {
        engine.clearLoopRange (Transport::Mode::song);
        return;
    }

    const auto stepsPerBar = Meter::of (document.getState()).stepsPerBar();

    engine.setLoopRangeSteps (Transport::Mode::song, (double) (bars.getStart() * stepsPerBar),
                              (double) (bars.getEnd() * stepsPerBar));
}

void MainComponent::updateMidiTargetChannel()
{
    const auto index = ProjectEdits::channelIndexForId (document.getState(),
                                                        editorState.getSelectedChannelId());

    // A selection pointing at nothing leaves the router where it was rather
    // than sending notes to channel 0 by accident.
    if (index >= 0)
        midiHost.getRouter().setTargetChannel (index);
}

void MainComponent::showMidiSettings()
{
    auto* panel = new MidiSettingsPanel (midiHost, nullptr);

    panel->onDevicesChanged = [this]
    { statusBar.showMessage (midiHost.describeInputs(), StatusBar::Severity::info); };

    dialog::launch (panel, "MIDI Settings", this);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    transportBar.setBounds (area.removeFromTop (tokens::size::stripTransport));
    statusBar.setBounds (area.removeFromBottom (tokens::size::stripStatus));

    const auto open = juce::jlimit (Settings::minPanelWidth,
                                    juce::jmax (Settings::minPanelWidth, area.getWidth() - 360),
                                    panelWidth);

    // Folded rather than switched: the panel's width is the open width scaled
    // by how far the fold has got, so the editor beside it grows into the space
    // at the same rate.
    const auto width = juce::roundToInt ((double) open * (1.0 - (double) collapse.get()));

    // Hidden as well as given no width, but only once it has ARRIVED: a
    // zero-width panel still lays its children out and still paints, and its
    // knobs would keep taking the clicks meant for the editor beside it -
    // while a panel hidden on the first frame of a fold just vanishes.
    instrumentPanel.setVisible (width > 0);
    instrumentPanel.setBounds (area.removeFromRight (width));

    // The divider stays whichever way the panel goes - it is what the panel is
    // brought back with.
    divider.setBounds (area.removeFromRight (dividerWidth));
    tabs.setBounds (area);
}

} // namespace dew
