// =============================================================================
// The two long-running jobs the window owns: recording, and rendering.
//
// The same class, a second translation unit - the shape PlaylistPaint.cpp
// already uses.
//
// Neither is an editor and neither belongs to one. Both start from a menu item,
// run somewhere other than the message thread, put something in the status bar
// while they run, and land their result in the document or on disk when they
// finish. Between them they read the audio host, the sample pool, the engine,
// the document, the editor state and the settings - which is exactly why they
// are a second file about this class rather than a collaborator taking six
// references.
// =============================================================================

#include "i18n/Strings.h"
#include "ui/MainComponent.h"

#include "engine/Metronome.h"
#include "io/AudioRecorder.h"
#include "io/RenderJob.h"
#include "io/SamplePool.h"
#include "model/AssetPaths.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "ui/AudioSettingsPanel.h"
#include "ui/DewDialog.h"
#include "ui/design/Tokens.h"

namespace dew
{

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

    if (! channel.isValid() || ! ProjectEdits::playsClips (channel))
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
                          : AssetPaths::nextTakeFile (
                                AssetPaths::sidecarFolderFor (document.getFile()),
                                channel[ids::name].toString());

    const auto numInputs = juce::jlimit (1, 2,
                                         device->getActiveInputChannels().countNumberOfSetBits());
    const auto meter = Meter::of (document.getState());
    const auto stepsPerBar = meter.stepsPerBar();
    const auto punchInBar = (int) (engine.getPlayheadSteps() / (double) stepsPerBar);

    // ONE number, handed to both, so the recorder and the engine cannot
    // disagree about which block the take starts on: each counts down by the
    // same numSamples in the same device callback. Constant tempo, which is the
    // assumption finishRecording already makes about a take's own length.
    const auto bpm = juce::jmax (1.0, (double) document.getState()[ids::tempoBpm]);
    const auto countIn = transportBar.isCountInEnabled()
                             ? countInSamplesFor (1, meter, bpm, device->getCurrentSampleRate())
                             : (juce::int64) 0;

    // The recorder BEFORE the engine, and that order is load-bearing: a device
    // callback landing between the two makes the recorder discard one block
    // more than the engine counted in, so the take starts a hair after the
    // music rather than before it.
    if (const auto error = recorder.start (file, device->getCurrentSampleRate(), numInputs,
                                           punchInBar, countIn);
        error.isNotEmpty())
        return error;

    // Rolling is what makes the take land where the playhead is, and what lets
    // it be played against the rest of the arrangement.
    engine.setMode (Transport::Mode::song);

    if (countIn > 0)
    {
        // A count-in's last click IS the downbeat the take starts on, so the
        // take has to start on one. The playhead is wherever it was left, which
        // may be mid-bar, while the clip is placed at punchInBar - a gap that
        // has always been there and was invisible until there was a click to
        // compare it against.
        //
        // Only on this path. Recording without a count-in keeps the behaviour
        // it has always had, because moving somebody's playhead is not
        // something a Record button should do unasked.
        engine.setPlayheadSteps ((double) punchInBar * (double) stepsPerBar);
        engine.playWithCountIn (countIn);
    }
    else
    {
        engine.play();
    }

    return {};
}

void MainComponent::finishRecording()
{
    auto& recorder = audioHost.getRecorder();

    engine.stop();

    const auto file = recorder.stop();

    if (file == juce::File() || ! file.existsAsFile())
    {
        statusBar.showMessage (tr (StringId::status_nothingRecorded), StatusBar::Severity::warning);
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
        statusBar.showMessage (tr (StringId::status_recordingUnreadable),
                               StatusBar::Severity::warning);
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
    const auto lengthBars = juce::jmax (
        1, (int) std::ceil ((double) entry.audio->getNumSamples() / samplesPerBar));

    auto& undo = document.getUndoManager();
    undo.beginNewTransaction ("Record audio");

    ProjectEdits::setSampleSource (channel, AssetPaths::relativise (file, document.getFile()),
                                   (int) entry.sourceSampleRate, entry.audio->getNumSamples(),
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

    statusBar.showMessage (
        tr (StringId::status_recorded,
            Args {}
                .with ("seconds", juce::String (entry.audio->getNumSamples()
                                                    / juce::jmax (1.0, entry.sourceSampleRate),
                                                1))
                .with ("channel", channel[ids::name].toString())),
        StatusBar::Severity::info);
}

void MainComponent::startRender (const RenderPanel::Request& request, Settings* settingsToUpdate)
{
    const auto startIn = settingsToUpdate != nullptr ? settingsToUpdate->getLastRenderDirectory()
                                                     : AssetPaths::defaultBrowseFolder();

    const auto extension = OfflineRenderer::extensionFor (request.options.format);

    // A directory when it is going to be several files, a file when it is one.
    // JUCE_MODAL_LOOPS_PERMITTED is 0, so this is the async form; the blocking
    // one asserts.
    auto chooser = std::make_shared<juce::FileChooser> (
        request.stems ? tr (StringId::file_chooseStemFolder) : tr (StringId::file_renderTo),
        startIn.getChildFile (request.suggestedName + (request.stems ? "" : extension)),
        request.stems ? juce::String() : "*" + extension);

    const auto flags = request.stems ? (juce::FileBrowserComponent::saveMode
                                        | juce::FileBrowserComponent::canSelectDirectories)
                                     : (juce::FileBrowserComponent::saveMode
                                        | juce::FileBrowserComponent::warnAboutOverwriting);

    chooser->launchAsync (
        flags,
        [this, chooser, request, settingsToUpdate] (const juce::FileChooser& result)
        {
            auto destination = result.getResult();

            if (destination == juce::File())
                return;

            if (! request.stems && destination.getFileExtension().isEmpty())
                destination = destination.withFileExtension (
                    OfflineRenderer::extensionFor (request.options.format));

            if (settingsToUpdate != nullptr)
            {
                settingsToUpdate->setLastRenderDirectory (
                    request.stems ? destination : destination.getParentDirectory());
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
            job.options.soundFontPool = &soundFontPool;

            statusBar.showMessage (
                tr (StringId::status_rendering, Args {}.with ("file", destination.getFileName())),
                StatusBar::Severity::info);

            const auto started = renderJob->start (
                std::move (job),
                [this] (const RenderReport& report)
                {
                    for (const auto& warning : report.warnings)
                        statusBar.showMessage (warning, StatusBar::Severity::warning);

                    if (report.cancelled)
                    {
                        // The user asked for this. Reporting it as an error would tell
                        // them their own click was a bug.
                        statusBar.showMessage (tr (StringId::status_renderCancelled),
                                               StatusBar::Severity::info);
                        return;
                    }

                    if (! report.ok())
                    {
                        statusBar.showMessage (report.result.getErrorMessage(),
                                               StatusBar::Severity::error);
                        return;
                    }

                    // A file NAME when there is one, and a count when there
                    // are several - so the count is a plural of its own rather
                    // than a number glued to an English word.
                    const auto what = report.files.size() == 1
                                          ? report.files[0].getFileName()
                                          : tr (StringId::status_renderedFiles,
                                                Args {}.with ("count", report.files.size()));

                    statusBar.showMessage (
                        tr (StringId::status_rendered,
                            Args {}
                                .with ("what", what)
                                .with ("peak",
                                       juce::String (juce::Decibels::gainToDecibels (report.peak),
                                                     1))),
                        StatusBar::Severity::success);
                });

            if (! started)
                statusBar.showMessage (tr (StringId::status_renderInProgress),
                                       StatusBar::Severity::warning);
        });
}

void MainComponent::showRenderDialog (Settings* settingsToUpdate)
{
    if (renderJob != nullptr && renderJob->isRunning())
    {
        statusBar.showMessage (tr (StringId::status_renderInProgress),
                               StatusBar::Severity::warning);
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

    dialog::launch (panel, tr (StringId::render_title), this);
}

void MainComponent::showAudioSettings()
{
    auto* panel = new AudioSettingsPanel (audioHost, engine);

    panel->onDeviceChanged = [this]
    { statusBar.showMessage (audioHost.describeDevice(), StatusBar::Severity::info); };

    dialog::launch (panel, tr (StringId::audio_title), this);
}
} // namespace dew
