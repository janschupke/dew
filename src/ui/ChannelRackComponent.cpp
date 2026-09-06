#include "ui/ChannelRackComponent.h"

#include "i18n/Strings.h"
#include "model/Ids.h"
#include "model/Meter.h"
#include "model/ProjectEdits.h"
#include "ui/design/Glyphs.h"
#include "ui/design/Tokens.h"
#include "ui/MenuSeam.h"
#include "ui/primitives/ButtonBehaviour.h"

namespace dew
{

using namespace tokens;

// -----------------------------------------------------------------------------

ChannelRackComponent::ChannelRackComponent (ProjectDocument& d, AudioEngine& e, EditorState& s,
                                            SamplePool* p)
    : document (d)
    , engine (e)
    , editorState (s)
    , grid (d, e, s, p)
{
    setComponentID ("channelRack");
    // A name and a PLACE in the tree a screen reader is given. focusContainer,
    // not keyboardFocusContainer: the two flags are independent, and the second
    // would confine the tab key to this panel with no key to leave it - a
    // keyboard trap, which is worse than the flat tab order it would tidy.
    setTitle (tr (StringId::channelRack_title));
    setFocusContainerType (FocusContainerType::focusContainer);

    confirmDestructive = confirmWithPanel (this);
    contentHolder.setComponentID ("channelRackContent");
    viewport.setComponentID ("channelRackViewport");

    // The channel rack had no ruler at all: no bar numbers, and nothing to
    // click to move the transport. It reads the grid's timeline rather than
    // keeping a copy, so the two cannot drift apart.
    ruler.timelineSource = [this]() -> const TimelineView& { return grid.getTimeline(); };

    ruler.styleSource = [this]
    {
        ruler::Style style;
        const auto meter = Meter::of (document.getState());
        style.stepsPerBar = meter.stepsPerBar();
        style.beatsPerBar = meter.beatsPerBar;
        style.totalSteps = grid.getNumSteps();
        style.playing = engine.isPlaying();

        if (engine.getMode() == Transport::Mode::pattern)
            style.playheadSteps = (double) ((int) engine.getPlayheadSteps() % style.totalSteps);

        // The same span the piano roll selects: both are views of one pattern,
        // so a loop taken out in one has to be visible in the other.
        if (editorState.hasStepSelection())
        {
            const auto selection = editorState.getSelectedStepRange();
            style.selectionStartSteps = (double) selection.getStart();
            style.selectionEndSteps = (double) selection.getEnd();
        }

        return style;
    };

    // The marker as well as the playhead, which is what makes a click on a
    // ruler say where playback BEGINS rather than only where it is now: pause
    // comes back here. setStartMarkerSteps moves both, so the two cannot
    // disagree and the ruler has one thing to draw.
    ruler.onSeek = [this] (double steps) { engine.setStartMarkerSteps (steps); };

    // The same span the piano roll writes, so the sequencer's ruler is no
    // longer the one place in the app where a loop can be seen but not chosen.
    ruler.onRangeChanged = [this] (juce::Range<int> steps)
    {
        editorState.setSelectedStepRange (steps);
        ruler.repaint();
    };

    ruler.onRangeCleared = [this]
    {
        editorState.clearStepSelection();
        ruler.repaint();
    };

    grid.onTimelineChanged = [this] { ruler.repaint(); };

    ruler.setComponentID ("channelRackRuler");
    addAndMakeVisible (ruler);

    zoomButtons.onZoom = [this] (double factor)
    {
        if (juce::exactlyEqual (factor, 0.0))
            grid.zoomToFit();
        else
            grid.zoomBy (factor, (float) grid.getWidth() * 0.5f);
    };

    addAndMakeVisible (zoomButtons);

    contentHolder.addAndMakeVisible (grid);
    viewport.setViewedComponent (&contentHolder, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    addChannelButton.onClick = [this] { addChannel(); };

    // The kind each button makes, said in a picture as well as in its word.
    addChannelButton.setGlyph (glyph::forInstrument (InstrumentType::synth));
    addAudioButton.setGlyph (glyph::forInstrument (InstrumentType::audio));
    addSoundFontButton.setGlyph (glyph::forInstrument (InstrumentType::soundfont));

    addAudioButton.onClick = [this] { addAudioChannel(); };
    addSoundFontButton.onClick = [this] { addSoundFontChannel(); };

    // Into the scrolling holder, not onto the panel: it is the next row of the
    // list, so it belongs to the list and scrolls with it.
    addChannelButton.setComponentID ("addChannelButton");
    addChannelButton.setTooltip (tr (StringId::channelRack_addSynth_help));
    contentHolder.addAndMakeVisible (addChannelButton);

    addAudioButton.setComponentID ("addAudioButton");
    addAudioButton.setTooltip (tr (StringId::channelRack_addAudio_help));
    contentHolder.addAndMakeVisible (addAudioButton);

    addSoundFontButton.setComponentID ("addSoundFontButton");
    addSoundFontButton.setTooltip (tr (StringId::channelRack_addSoundFont_help));
    contentHolder.addAndMakeVisible (addSoundFontButton);

    document.getState().addListener (this);
    editorState.addChangeListener (this);

    rebuildHeaders();
}

ChannelRackComponent::~ChannelRackComponent()
{
    document.getState().removeListener (this);
    editorState.removeChangeListener (this);
}

void ChannelRackComponent::setParamMenuHost (const paramMenu::Host* host)
{
    paramMenuHost = host;

    // The rows exist before the host does - they are built in the constructor
    // and the host is handed down afterwards - so this re-attaches rather than
    // only recording the pointer for the next rebuild.
    for (auto* header : headers)
        header->attachParamMenus (host);
}

void ChannelRackComponent::refresh()
{
    rebuildHeaders();
}

void ChannelRackComponent::addChannelOfType (InstrumentType type)
{
    // The three add functions differ in what they arm and what they select
    // afterwards, not only in what they create, so this dispatches to them
    // rather than reaching past them to one parameterised edit.
    switch (type)
    {
        case InstrumentType::synth: addChannel(); return;
        case InstrumentType::audio: addAudioChannel(); return;
        case InstrumentType::soundfont: addSoundFontChannel(); return;
    }

    jassertfalse;
}

void ChannelRackComponent::addChannel()
{
    auto& undo = document.getUndoManager();
    undo.beginNewTransaction ("Add channel");
    const auto channel = ProjectEdits::addChannel (document.getState(), {}, &undo);
    editorState.setSelectedChannelId ((int) channel[ids::id]);
}

void ChannelRackComponent::addAudioChannel()
{
    auto& undo = document.getUndoManager();
    undo.beginNewTransaction ("Add audio channel");
    const auto channel = ProjectEdits::addAudioChannel (document.getState(), {}, &undo);

    // Selected AND armed: adding an audio channel is something you do in order
    // to record into it, and leaving it disarmed makes Record answer with an
    // error message about a step the user has just taken.
    editorState.setSelectedChannelId ((int) channel[ids::id]);
    editorState.setArmedChannelId ((int) channel[ids::id]);
}

void ChannelRackComponent::addSoundFontChannel()
{
    auto& undo = document.getUndoManager();
    undo.beginNewTransaction ("Add soundfont channel");
    const auto channel = ProjectEdits::addSoundFontChannel (document.getState(), {}, &undo);

    // Selected, and NOT armed: a soundfont channel is played rather than
    // recorded into, and the next thing to do with it is choose a file - which
    // is in the instrument panel that selecting it opens.
    editorState.setSelectedChannelId ((int) channel[ids::id]);
}

void ChannelRackComponent::removeChannel (int channelId)
{
    const auto channel = ProjectEdits::findChannel (document.getState(), channelId);

    if (! channel.isValid())
        return;

    ConfirmPanel::Request request;
    request.title = tr (StringId::dialog_removeChannel_title);
    request.message = tr (StringId::dialog_removeChannel_body,
                          Args {}.with ("name", channel[ids::name].toString()));

    // By id, resolved again on the way back: the dialog is async and the
    // document may have moved on by the time the answer arrives.
    confirmDestructive (request,
                        [this, channelId]
                        {
                            auto found = ProjectEdits::findChannel (document.getState(), channelId);

                            if (! found.isValid())
                                return;

                            auto& undo = document.getUndoManager();
                            undo.beginNewTransaction ("Remove channel");
                            ProjectEdits::removeChannel (document.getState(), found, &undo);
                        });
}

bool ChannelRackComponent::applyChannelMenuChoice (int channelId, int choice)
{
    for (auto* header : headers)
        if (header->getChannelId() == channelId)
        {
            header->applyMenuChoice (choice);
            return true;
        }

    return false;
}

juce::StringArray ChannelRackComponent::channelMenuItems (int channelId) const
{
    for (const auto* header : headers)
        if (header->getChannelId() == channelId)
        {
            const auto menu = header->buildMenu();

            return menuItems (menu);
        }

    return {};
}

void ChannelRackComponent::rebuildHeaders()
{
    headers.clear();

    for (const auto& channel : document.getState())
        if (channel.hasType (ids::CHANNEL))
            headers.add (new ChannelRackHeader (document, editorState, channel))
                ->attachParamMenus (paramMenuHost);

    for (auto* header : headers)
    {
        header->onAddChannel = [this] (InstrumentType type) { addChannelOfType (type); };
        header->onRemoveChannel = [this] (int id) { removeChannel (id); };
        contentHolder.addAndMakeVisible (header);

        // A freshly built row has to be brought up to date once, not only on
        // the next property change: everything a row shows CONDITIONALLY - the
        // arm toggle on an audio channel - is otherwise invisible until
        // something unrelated happens to touch the channel.
        header->refresh();
    }

    resized();
    repaint();
}

void ChannelRackComponent::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child)
{
    if (child.hasType (ids::CHANNEL))
        rebuildHeaders();
    else if (child.hasType (ids::NOTE))
        grid.repaint();
}

void ChannelRackComponent::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& child, int)
{
    if (child.hasType (ids::CHANNEL))
        rebuildHeaders();
    else if (child.hasType (ids::NOTE))
        grid.repaint();
}

void ChannelRackComponent::valueTreePropertyChanged (juce::ValueTree& tree,
                                                     const juce::Identifier& property)
{
    if (tree.hasType (ids::CHANNEL))
    {
        for (auto* header : headers)
            if (header->getChannelId() == (int) tree[ids::id])
                header->refresh();

        grid.repaint();
    }
    else if (tree.hasType (ids::PATTERN) || tree.hasType (ids::NOTE))
    {
        if (property == ids::lengthSteps)
            resized();

        grid.repaint();
    }
    else if (tree.hasType (ids::PROJECT))
    {
        // The rack had no branch for the project node at all, so a change to
        // the metre - or to the tempo, or to stepsPerBeat - left its bar
        // shading and its ruler drawn to the old grid until something else
        // happened to repaint them. resized() as well as a repaint, because the
        // ruler's Style is rebuilt during layout.
        resized();
        grid.repaint();
        repaint();
    }
}

void ChannelRackComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    for (auto* header : headers)
        header->repaint();

    grid.repaint();
}

void ChannelRackComponent::paint (juce::Graphics& g)
{
    g.fillAll (colour::background);

    // The header column continues below the last channel so the split between
    // names and steps stays readable down the whole panel. It runs the full
    // height now that the add button is a row of the list rather than a footer.
    g.setColour (colour::surface.withAlpha (emphasis::subdued));
    g.fillRect (0, 0, size::gutterChannel, getHeight());

    g.setColour (colour::dividerStrong);
    g.drawVerticalLine (size::gutterChannel, 0.0f, (float) getHeight());
}

void ChannelRackComponent::resized()
{
    auto area = getLocalBounds();

    // The ruler spans the step columns only; the header column keeps its own
    // corner, which the shared ruler knows nothing about.
    auto rulerStrip = area.removeFromTop (size::rulerHeight);

    // The corner beside the ruler, which the ruler itself does not draw into.
    zoomButtons.setBounds (
        rulerStrip.withWidth (ZoomButtons::preferredWidth)
            .withX (size::gutterChannel - ZoomButtons::preferredWidth - space::sm));

    ruler.setBounds (rulerStrip.withTrimmedLeft (size::gutterChannel));

    viewport.setBounds (area);

    // Content is at least as tall as the viewport, so the grid always fills the
    // panel and can paint the region below the rows as inert. The add button is
    // counted as a row of its own, or it would be unreachable the moment the
    // channels overflow the viewport.
    const auto rowsHeight = (headers.size() + 1) * size::rowHeight;
    const auto visibleHeight = viewport.getMaximumVisibleHeight();
    const auto contentHeight = juce::jmax (visibleHeight, rowsHeight);

    contentHolder.setSize (viewport.getMaximumVisibleWidth(), contentHeight);

    for (int i = 0; i < headers.size(); ++i)
        headers[i]->setBounds (0, i * size::rowHeight, size::gutterChannel, size::rowHeight);

    // Directly below the last channel, spanning the header column: the next
    // empty row of the list, where the channel it adds will appear. The two
    // kinds share that row rather than stacking, so the grid still starts one
    // row after the last channel.
    auto addRow = juce::Rectangle<int> (0, headers.size() * size::rowHeight, size::gutterChannel,
                                        size::rowHeight)
                      .reduced (space::sm, space::xs);

    // Three across what was two: a third of the row each, with the same gap
    // between them the pair had.
    const auto third = (addRow.getWidth() - space::xs * 2) / 3;

    addChannelButton.setBounds (addRow.removeFromLeft (third));
    addRow.removeFromLeft (space::xs);
    addAudioButton.setBounds (addRow.removeFromLeft (third));
    addRow.removeFromLeft (space::xs);
    addSoundFontButton.setBounds (addRow);

    grid.setBounds (size::gutterChannel, 0,
                    juce::jmax (120, contentHolder.getWidth() - size::gutterChannel),
                    contentHeight);
}

} // namespace dew
