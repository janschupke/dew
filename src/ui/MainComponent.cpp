#include "ui/MainComponent.h"
#include "i18n/Strings.h"
#include "ui/DewDialog.h"

#include <cmath>

#include "model/AssetPaths.h"
#include "model/Meter.h"

#include "ui/AudioSettingsPanel.h"
#include "ui/MidiSettingsPanel.h"

#include "model/ProjectEdits.h"
#include "ui/design/Cursors.h"
#include "ui/design/FocusGroups.h"
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

    // A preset row says what it is for in the strip along the bottom, the way
    // every other control in the window does. It cannot get there by itself:
    // HoverHelp listens to this component's tree, and a popup menu is a
    // separate window that tree does not contain.
    const auto sayWhatThePresetIsFor = [this] (const juce::String& help)
    { statusBar.setHoverHelp (help); };

    instrumentPanel.setPresetHoverSink (sayWhatThePresetIsFor);
    tabs.setPresetHoverSink (sayWhatThePresetIsFor);

    // A layout, not a paint.
    collapse.onChanged = [this] { resized(); };

    // An effect card opening makes the panel taller than the viewport showing
    // it, and only the viewport's owner can do anything about that. The chain
    // used to scroll inside a viewport of its own, nested in this one, so the
    // growth went nowhere and the sidebar never learned there was more to see.
    instrumentPanel.onRequiredHeightChanged = [this] { resized(); };

    juce::Desktop::getInstance().setDefaultLookAndFeel (&lookAndFeel);

    // Before the first projectChanged(), or the opening snapshot would resolve
    // every audio channel to silence.
    engine.setSamplePool (&samplePool);
    engine.setSoundFontPool (&soundFontPool);

    addAndMakeVisible (transportBar);
    addAndMakeVisible (tabs);

    // Viewed rather than parented: see panelViewport's own comment. `false` -
    // the viewport does not own the panel, MainComponent does.
    panelViewport.setComponentID ("instrumentPanelViewport");
    panelViewport.setViewedComponent (&instrumentPanel, false);
    panelViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (panelViewport);

    // Any document change schedules a snapshot rebuild. Coalescing through the
    // AsyncUpdater means a knob drag costs one rebuild per message-loop turn
    // rather than one per mouse move.
    document.onProjectChanged = [this] { triggerAsyncUpdate(); };

    // Push the initial project before opening the device, so the first block
    // the engine renders already has something in it.
    projectChanged();

    addAndMakeVisible (statusBar);
    addAndMakeVisible (divider);

    // Parked in the tab strip, which reserves width for it. It was on the
    // divider, which straddles the seam and is raised above both sides - so it
    // sat half over the last tab and half over the panel's title band.
    panelToggle.setComponentID ("panelToggle");
    panelToggle.setMouseClickGrabsKeyboardFocus (false);
    panelToggle.onClick = [this] { setPanelCollapsed (! panelCollapsed); };
    tabs.setTabStripTrailing (&panelToggle);

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

    // The letter keys as an instrument - see MainComponentKeyboard.cpp.
    wireKeyboardInput();

    // The button does the engine's half itself, so this is only what it cannot
    // reach - which is the same second half panic() does for the key and the
    // menu. reset(), never MidiRouter::panic(): see MainComponent::panic.
    transportBar.onPanic = [this] { midiHost.getRouter().reset(); };

    transportBar.onMeterChanged = [this] (bool wasExact)
    {
        const auto meter = Meter::of (document.getState());

        // Said once, here, rather than left to be noticed: redefining a bar
        // moves every clip, and a ratio that does not divide evenly has to
        // round one to the nearest whole bar.
        if (wasExact)
            statusBar.showMessage (
                tr (StringId::status_meterChanged, Args {}.with ("meter", meter.toString())),
                StatusBar::Severity::info);
        else
            statusBar.showMessage (
                tr (StringId::status_meterChangedRounded, Args {}.with ("meter", meter.toString())),
                StatusBar::Severity::warning);
    };

    // Which channel MIDI plays follows the selection, and has to be pushed to
    // the router as an index because the MIDI thread cannot read EditorState.
    editorState.addChangeListener (this);
    updateMidiTargetChannel();
    updateLoopRange();

    if (! openAudioDevice)
    {
        statusBar.showMessage (tr (StringId::status_audioNotOpened), StatusBar::Severity::warning);
    }
    else if (const auto error = audioHost.start(); error.isNotEmpty())
    {
        statusBar.showMessage (
            tr (StringId::status_audioUnavailable, Args {}.with ("error", error)),
            StatusBar::Severity::error);
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

void MainComponent::pointPoolsAtDocument()
{
    // A relative audio or soundfont path can only be resolved against where the
    // document lives, and Save As moves that out from under it.
    samplePool.setProjectFile (document.getFile());
    soundFontPool.setProjectFile (document.getFile());
}

void MainComponent::projectChanged()
{
    // Before the snapshot, for the reason above.
    pointPoolsAtDocument();

    juce::StringArray warnings;
    engine.setProject (document.getState(), &warnings);

    // Channels may have been added or removed, which moves every index after
    // them - including the one MIDI is pointed at. Removing the SELECTED one
    // leaves a dangling id: EditorState is deliberately not in the ValueTree,
    // so nothing listens for the channel it names going away.
    resolveSelectedChannel();
    updateMidiTargetChannel();
    updateLoopRange();

    // A project that cannot be rendered as the user expects is worth saying so
    // once rather than silently playing something else. It expires on its own
    // now instead of standing until something overwrites it.
    if (! warnings.isEmpty())
        statusBar.showMessage (warnings[0], StatusBar::Severity::warning);
}

void MainComponent::resolveSelectedChannel()
{
    const auto project = document.getState();

    if (ProjectEdits::findChannel (project, editorState.getSelectedChannelId()).isValid())
        return;

    for (const auto& channel : project)
    {
        if (! channel.hasType (ids::CHANNEL))
            continue;

        editorState.setSelectedChannelId ((int) channel[ids::id]);
        return;
    }
}

void MainComponent::documentWasReplaced()
{
    document.onProjectChanged = [this] { triggerAsyncUpdate(); };

    // Before the refreshes: every one of them reads the selection, and a panel
    // refreshed against a channel that is not there disables itself and stays
    // disabled until something else touches it.
    resolveSelectedChannel();

    // And before them too, which is the fix rather than a tidy-up. The pools
    // used to be pointed at the document inside projectChanged, three lines
    // BELOW this - so the instrument panel resolved a relative soundfont path
    // against the file the last project lived in, found nothing, and drew a
    // channel whose font was "not on this machine" with an empty preset list.
    // Nothing refreshed it afterwards, so opening a project was enough to lose
    // a soundfont that was sitting right there.
    pointPoolsAtDocument();

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

void MainComponent::showLanguageNotice()
{
    statusBar.showMessage (tr (StringId::status_languageChanged), StatusBar::Severity::info);
}

void MainComponent::showAudioRestoreFailure (const juce::String& error)
{
    statusBar.showMessage (tr (StringId::status_audioNotRestored, Args {}.with ("error", error)),
                           StatusBar::Severity::warning);
}

void MainComponent::showCrashNotice (const juce::File& logFolder)
{
    statusBar.showMessage (
        tr (StringId::status_crashedLastTime, Args {}.with ("folder", logFolder.getFullPathName())),
        StatusBar::Severity::warning);
}

void MainComponent::showMcpUnavailable()
{
    statusBar.showMessage (tr (StringId::status_mcpUnavailable), StatusBar::Severity::error);
}

void MainComponent::showLoadWarnings (const juce::StringArray& warnings)
{
    if (warnings.isEmpty())
        return;

    statusBar.showMessage (
        tr (StringId::status_loadWarnings,
            Args {}.count ((juce::int64) warnings.size()).with ("first", warnings[0])),
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
}

void MainComponent::PanelDivider::mouseDown (const juce::MouseEvent& event)
{
    // The right button moves nothing here either. A latch rather than a second
    // read of the modifiers on the drag, for the reason PopupPress gives.
    popupPressed = event.mods.isPopupMenu();

    if (popupPressed)
        return;

    widthAtDragStart = owner.panelWidth;
}

void MainComponent::PanelDivider::mouseDrag (const juce::MouseEvent& event)
{
    if (popupPressed)
        return;

    // No un-folding here any more: the seam is hidden while the panel is
    // folded, so a drag cannot start on one. It was the other half of a handle
    // that was always present - grab a seam with nothing behind it and the
    // panel sprang open under the pointer.
    owner.setPanelWidth (widthAtDragStart - event.getDistanceFromDragStartX());
}

void MainComponent::updatePanelToggle()
{
    panelToggle.setIcon (panelCollapsed ? icons::chevronLeft() : icons::chevronRight());
    panelToggle.setTooltip (
        tr (panelCollapsed ? StringId::shell_showPanel_help : StringId::shell_hidePanel_help));
}

bool MainComponent::PanelDivider::hitTest (int x, int)
{
    // A band either side of the rule, and nothing else. The rest of this
    // component is over the editor and over the panel, and belongs to them:
    // without this it would take every click along the panel's left edge and
    // there would be nothing on screen to say why.
    const auto rule = getWidth() / 2;

    return x >= rule - tokens::space::xs && x <= rule + tokens::space::xs;
}

void MainComponent::PanelDivider::paint (juce::Graphics& g)
{
    // Transparent. This straddles the seam rather than reserving a column of
    // its own, so the editor and the panel meet under it and the only thing
    // drawn here is the rule they meet on.
    //
    // It runs the full height now, THROUGH the chevron rather than starting
    // below it: the button is a small opaque rectangle sitting on the line, so
    // the line reads as continuous behind it rather than as a rule with a hole.
    g.setColour (isMouseOverOrDragging() ? tokens::colour::accent : tokens::colour::dividerStrong);
    g.drawVerticalLine (getWidth() / 2, 0.0f, (float) getHeight());
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
    // The VIEWPORT's width, which is what the panel occupies. The panel inside
    // it is a scrolled component and keeps a width of its own while folded.
    return panelViewport.getWidth();
}

void MainComponent::setPanelCollapsed (bool collapsed)
{
    if (collapsed == panelCollapsed)
        return;

    panelCollapsed = collapsed;
    updatePanelToggle();
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

void MainComponent::focusAdjacentGroup (int delta)
{
    focusGroups::moveFocus (*this, delta);
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

void MainComponent::panic()
{
    engine.panic();

    // reset(), never MidiRouter::panic(): the latter writes the MIDI ring and
    // the note bookkeeping directly and belongs to the MIDI thread alone - see
    // its header. This asks the MIDI thread to clear its own next time it
    // wakes, which is why it is the one that is public.
    midiHost.getRouter().reset();
}

void MainComponent::useSettings (Settings& s)
{
    // The one thing in the window that writes back: the soundfont chooser,
    // which remembers the folder somebody browsed to. Handed straight down
    // rather than held here as well, so there is one owner of the pointer.
    instrumentPanel.setSettings (&s);
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateMidiTargetChannel();

    // A key held while the selection moves has already been sent to the old
    // channel, and the note-off would go to the new one - so let go of
    // everything here, the same way the MIDI target is re-pointed.
    typingKeyboard.releaseAll();

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

    dialog::launch (panel, tr (StringId::midi_title), this);
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
    panelViewport.setVisible (width > 0);
    panelViewport.setBounds (area.removeFromRight (width));

    // The greater of what there is and what the panel needs. Equal to the
    // viewport at any ordinary size, so no scrollbar appears and nothing moves;
    // taller when the window is too short, so the chain scrolls into reach
    // instead of being handed an empty rectangle.
    if (width > 0)
    {
        // Decided from the viewport's BOUNDS, not from what it is currently
        // showing. getMaximumVisibleWidth() is already reduced by a scrollbar
        // that is up, so sizing from it while deciding whether that scrollbar
        // is needed is a loop that settles on a bar nobody asked for - a stray
        // ten pixels down the panel at every window size.
        const auto required = instrumentPanel.getRequiredHeight();
        const auto available = panelViewport.getHeight();
        const auto scrolls = required > available;

        instrumentPanel.setSize (panelViewport.getWidth()
                                     - (scrolls ? panelViewport.getScrollBarThickness() : 0),
                                 juce::jmax (available, required));
    }

    // The editor takes the rest, right up to the panel: there is no column
    // between them any more.
    tabs.setBounds (area);

    // And the seam STRADDLES the boundary they now share, on top of both. It
    // stays whichever way the panel goes - it is what the panel is brought back
    // with - so it is anchored on the panel's left edge rather than taken out
    // of the editor's width.
    // Clamped so a folded panel does not take the chevron off the edge with it:
    // the toggle is the only way back.
    const auto seamX = juce::jlimit (0, juce::jmax (0, getWidth() - seamWidth),
                                     area.getRight() - seamWidth / 2);

    // Not shown at all while the panel is folded. There is nothing on the other
    // side of the seam to resize, so it clamped against the window edge and sat
    // there as a live resize-cursor drag target on top of the editor - a grab
    // handle for a thing that is not on screen. The chevron is the way back,
    // which is what the clamp above exists to keep reachable.
    divider.setVisible (! panelCollapsed);
    divider.setBounds (seamX, area.getY(), seamWidth, area.getHeight());

    if (! panelCollapsed)
        divider.toFront (false);

    // Behind the divider, so the rule reads as continuous: the chevron is the
    // tab strip's child and the seam is drawn on top of everything.
    panelToggle.toBack();
}

} // namespace dew
