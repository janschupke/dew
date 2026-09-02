#include "ui/MainComponent.h"

#include <cmath>

#include "model/AssetPaths.h"
#include "model/Meter.h"

#include "ui/AudioSettingsPanel.h"
#include "ui/MidiSettingsPanel.h"

#include "model/ProjectEdits.h"
#include "ui/design/Tokens.h"

#include "model/Ids.h"
#include "ui/design/Animator.h"

namespace dew
{

MainComponent::MainComponent (bool openAudioDevice)
    : audioHost (engine),
      midiHost (audioHost.getDeviceManager(), engine),
      transportBar (document, engine, editorState),
      tabs (document, engine, editorState, &samplePool),
      instrumentPanel (document, editorState, &samplePool),
      statusBar (document, editorState, audioHost)
{
    juce::Desktop::getInstance().setDefaultLookAndFeel (&lookAndFeel);

    // Before the first projectChanged(), or the opening snapshot would resolve
    // every audio channel to silence.
    engine.setSamplePool (&samplePool);

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

MainComponent::PanelDivider::PanelDivider (MainComponent& o) : owner (o)
{
    setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);

    setComponentID ("panelDivider");

    toggleButton.setComponentID ("panelToggle");
    toggleButton.setWantsKeyboardFocus (false);
    toggleButton.setMouseCursor (juce::MouseCursor::PointingHandCursor);
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
    toggleButton.setBounds (getLocalBounds().removeFromTop (tokens::size::iconButton)
                                            .reduced (0, tokens::space::xxs));
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

void MainComponent::setPanelCollapsed (bool collapsed)
{
    if (collapsed == panelCollapsed)
        return;

    panelCollapsed = collapsed;
    divider.updateToggle();
    resized();
}

void MainComponent::applySettings (const Settings& settings)
{
    panelWidth = settings.getPanelWidth();
    panelCollapsed = settings.getPanelCollapsed();
    divider.updateToggle();

    Animator::shared().setReduceMotion (settings.getReduceMotion());

    editorState.setSelectedChannelId (settings.getSelectedChannelId());
    editorState.setSelectedMixerTrackId (settings.getSelectedMixerTrackId());
    editorState.setCurrentPatternId (settings.getCurrentPatternId());

    tabs.setCurrentTabIndex (settings.getTabIndex(), false);
    tabs.applyPianoRollView (settings.getPianoRollZoom(), settings.getPianoRollScroll(),
                             settings.getPianoRollPitchScroll());
    tabs.setPianoRollSnap (settings.getPianoRollSnap());

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
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateMidiTargetChannel();
    updateLoopRange();
}

bool MainComponent::isRecording() const noexcept
{
    return audioHost.getRecorder().isRecording();
}

juce::String MainComponent::toggleRecording()
{
    auto& recorder = audioHost.getRecorder();

    if (recorder.isRecording())
    {
        finishRecording();
        return {};
    }

    const auto channel = ProjectEdits::findChannel (document.getState(),
                                                    editorState.getArmedChannelId());

    if (! channel.isValid() || ! ProjectEdits::isAudioChannel (channel))
        return "Arm an audio channel first: add one with + Audio, then click its R button.";

    // Asked for here rather than at startup, so the microphone prompt arrives
    // attached to the thing that needs it.
    if (const auto error = audioHost.setInputEnabled (true); error.isNotEmpty())
        return error;

    auto* device = audioHost.getDeviceManager().getCurrentAudioDevice();

    if (device == nullptr)
        return "No audio device is running.";

    // Staging until the project has a file of its own; saving gathers it into
    // the sidecar folder. Recording into an unsaved project has to work - it is
    // how most first takes happen.
    const auto file = document.getFile() == juce::File()
                          ? AssetPaths::nextTakeFile (AssetPaths::stagingFolder(),
                                                      channel[ids::name].toString())
                          : AssetPaths::nextTakeFile (AssetPaths::sidecarFolderFor (document.getFile()),
                                                      channel[ids::name].toString());

    const auto numInputs = juce::jlimit (1, 2, device->getActiveInputChannels().countNumberOfSetBits());
    const auto stepsPerBar = Meter::of (document.getState()).stepsPerBar();
    const auto punchInBar = (int) (engine.getPlayheadSteps() / (double) stepsPerBar);

    if (const auto error = recorder.start (file, device->getCurrentSampleRate(), numInputs, punchInBar);
        error.isNotEmpty())
        return error;

    // Rolling is what makes the take land where the playhead is, and what lets
    // it be played against the rest of the arrangement.
    engine.setMode (Transport::Mode::song);
    engine.play();
    return {};
}

void MainComponent::finishRecording()
{
    auto& recorder = audioHost.getRecorder();

    engine.stop();

    const auto file = recorder.stop();

    if (file == juce::File() || ! file.existsAsFile())
    {
        statusBar.showMessage ("Nothing was recorded - check the input in Audio Settings.",
                               StatusBar::Severity::warning);
        return;
    }

    auto channel = ProjectEdits::findChannel (document.getState(), editorState.getArmedChannelId());

    if (! channel.isValid())
        return;

    // The file was only just written, so anything cached under this path is the
    // take before it.
    samplePool.forget (file);
    const auto& entry = samplePool.load (file);

    if (! entry.isValid())
    {
        statusBar.showMessage ("The recording could not be read back.", StatusBar::Severity::warning);
        return;
    }

    // A bar is however many beats the meter says, measured in the SOURCE's own
    // frames because that is what the take was captured in. This used to read
    // 240.0 / bpm, with the four beats folded into the constant, which made
    // every take a quarter too long in 3/4.
    const auto bpm = juce::jmax (1.0, (double) document.getState()[ids::tempoBpm]);
    const auto beatsPerBar = Meter::of (document.getState()).beatsPerBar;
    const auto samplesPerBar = juce::jmax (1.0, (60.0 * (double) beatsPerBar / bpm)
                                                    * entry.sourceSampleRate);

    // Rounded up: a take that runs a hair past a bar line needs the whole next
    // bar, or its tail would be cut by the clip that contains it.
    const auto lengthBars = juce::jmax (1, (int) std::ceil ((double) entry.audio->getNumSamples() / samplesPerBar));

    auto& undo = document.getUndoManager();
    undo.beginNewTransaction ("Record audio");

    ProjectEdits::setSampleSource (channel,
                                   AssetPaths::relativise (file, document.getFile()),
                                   (int) entry.sourceSampleRate,
                                   entry.audio->getNumSamples(),
                                   &undo);

    // One clip, on the first playlist track that has room, so a take is visible
    // in the arrangement rather than only audible from the channel rack.
    const auto playlist = document.getState().getChildWithName (ids::PLAYLIST);
    const auto punchInBar = recorder.getPunchInBar();

    for (auto track : playlist)
    {
        if (! track.hasType (ids::PLAYLIST_TRACK))
            continue;

        if (ProjectEdits::findClipAtBar (track, punchInBar).isValid())
            continue;

        ProjectEdits::addAudioClip (track, (int) channel[ids::id], punchInBar, lengthBars, &undo);
        ProjectEdits::growSongToFitClips (document.getState(), &undo);
        break;
    }

    statusBar.showMessage ("Recorded " + juce::String (entry.audio->getNumSamples() / juce::jmax (1.0, entry.sourceSampleRate), 1)
                               + " s into " + channel[ids::name].toString() + ".",
                           StatusBar::Severity::info);
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
        engine.setLoopRangeSteps (Transport::Mode::pattern,
                                  (double) steps.getStart(), (double) steps.getEnd());

    const auto bars = editorState.getSelectedBarRange();

    if (bars.isEmpty())
    {
        engine.clearLoopRange (Transport::Mode::song);
        return;
    }

    const auto stepsPerBar = Meter::of (document.getState()).stepsPerBar();

    engine.setLoopRangeSteps (Transport::Mode::song,
                              (double) (bars.getStart() * stepsPerBar),
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
    {
        statusBar.showMessage (midiHost.describeInputs(), StatusBar::Severity::info);
    };

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (panel);
    options.dialogTitle = "MIDI Settings";
    options.dialogBackgroundColour = tokens::colour::background;
    options.componentToCentreAround = this;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

void MainComponent::startRender (const RenderPanel::Request& request, Settings* settingsToUpdate)
{
    const auto startIn = settingsToUpdate != nullptr
                           ? settingsToUpdate->getLastRenderDirectory()
                           : juce::File::getSpecialLocation (juce::File::userMusicDirectory);

    const auto extension = OfflineRenderer::extensionFor (request.options.format);

    // A directory when it is going to be several files, a file when it is one.
    // JUCE_MODAL_LOOPS_PERMITTED is 0, so this is the async form; the blocking
    // one asserts.
    auto chooser = std::make_shared<juce::FileChooser> (
        request.stems ? "Choose a folder for the stems" : "Render to",
        startIn.getChildFile (request.suggestedName + (request.stems ? "" : extension)),
        request.stems ? juce::String() : "*" + extension);

    const auto flags = request.stems
                         ? (juce::FileBrowserComponent::saveMode
                            | juce::FileBrowserComponent::canSelectDirectories)
                         : (juce::FileBrowserComponent::saveMode
                            | juce::FileBrowserComponent::warnAboutOverwriting);

    chooser->launchAsync (flags, [this, chooser, request, settingsToUpdate]
                                 (const juce::FileChooser& result)
    {
        auto destination = result.getResult();

        if (destination == juce::File())
            return;

        if (! request.stems && destination.getFileExtension().isEmpty())
            destination = destination.withFileExtension (
                OfflineRenderer::extensionFor (request.options.format));

        if (settingsToUpdate != nullptr)
        {
            settingsToUpdate->setLastRenderDirectory (request.stems ? destination
                                                                    : destination.getParentDirectory());
            settingsToUpdate->setRenderFormat ((int) request.options.format);
            settingsToUpdate->setRenderSampleRate ((int) request.options.sampleRate);
            settingsToUpdate->setRenderBitDepth (request.options.bitDepth);
            settingsToUpdate->setRenderTailSeconds (request.options.tailSeconds);
            settingsToUpdate->setRenderNormalize (request.options.normalize);
        }

        if (renderJob == nullptr)
            renderJob = std::make_unique<RenderJob>();

        RenderJob::Request job;
        job.project = document.getState();
        job.destination = destination;
        job.options = request.options;
        job.stems = request.stems;

        // Without this a song containing recordings exports as the synth parts
        // alone, and says nothing about it. The pool outlives the job - see the
        // declaration order in the header.
        job.options.samplePool = &samplePool;

        statusBar.showMessage ("Rendering " + destination.getFileName() + "...",
                               StatusBar::Severity::info);

        const auto started = renderJob->start (std::move (job), [this] (const RenderReport& report)
        {
            for (const auto& warning : report.warnings)
                statusBar.showMessage (warning, StatusBar::Severity::warning);

            if (report.cancelled)
            {
                // The user asked for this. Reporting it as an error would tell
                // them their own click was a bug.
                statusBar.showMessage ("Render cancelled.", StatusBar::Severity::info);
                return;
            }

            if (! report.ok())
            {
                statusBar.showMessage (report.result.getErrorMessage(), StatusBar::Severity::error);
                return;
            }

            const auto what = report.files.size() == 1
                                ? report.files[0].getFileName()
                                : juce::String (report.files.size()) + " files";

            statusBar.showMessage ("Rendered " + what + "  ·  peak "
                                       + juce::String (juce::Decibels::gainToDecibels (report.peak), 1)
                                       + " dB",
                                   StatusBar::Severity::success);
        });

        if (! started)
            statusBar.showMessage ("A render is already going.", StatusBar::Severity::warning);
    });
}

void MainComponent::showRenderDialog (Settings* settingsToUpdate)
{
    if (renderJob != nullptr && renderJob->isRunning())
    {
        statusBar.showMessage ("A render is already going.", StatusBar::Severity::warning);
        return;
    }

    // Edits are coalesced through an AsyncUpdater, so a render started straight
    // after an edit would otherwise use the snapshot from before it.
    flushPendingEngineUpdate();

    auto* panel = new RenderPanel (document, editorState, settingsToUpdate);

    // The window owns the panel and deletes itself when its modal state ends, so
    // the panel closes itself by finding it rather than by holding a pointer to
    // something that will be gone. The deletion is deferred, which is what makes
    // this safe to call from inside one of the panel's own button callbacks.
    const auto close = [panel]
    {
        if (auto* window = panel->findParentComponentOfClass<juce::DialogWindow>())
            window->exitModalState (0);
    };

    panel->onClose = close;

    panel->onRender = [this, settingsToUpdate, close] (const RenderPanel::Request& request)
    {
        startRender (request, settingsToUpdate);
        close();
    };

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (panel);
    options.dialogTitle = "Render";
    options.dialogBackgroundColour = tokens::colour::background;
    options.componentToCentreAround = this;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

void MainComponent::showAudioSettings()
{
    auto* panel = new AudioSettingsPanel (audioHost, engine);

    panel->onDeviceChanged = [this]
    {
        statusBar.showMessage (audioHost.describeDevice(), StatusBar::Severity::info);
    };

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (panel);
    options.dialogTitle = "Audio Settings";
    options.dialogBackgroundColour = tokens::colour::background;
    options.componentToCentreAround = this;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    transportBar.setBounds (area.removeFromTop (46));
    statusBar.setBounds (area.removeFromBottom (StatusBar::barHeight));

    const auto width = panelCollapsed
                           ? 0
                           : juce::jlimit (Settings::minPanelWidth,
                                           juce::jmax (Settings::minPanelWidth, area.getWidth() - 360),
                                           panelWidth);

    // Hidden as well as given no width: a zero-width panel still lays its
    // children out and still paints, and its knobs would keep taking the
    // clicks meant for the editor beside it.
    instrumentPanel.setVisible (! panelCollapsed);
    instrumentPanel.setBounds (area.removeFromRight (width));

    // The divider stays whichever way the panel goes - it is what the panel is
    // brought back with.
    divider.setBounds (area.removeFromRight (dividerWidth));
    tabs.setBounds (area);
}

} // namespace dew
