#include "ui/PlaylistComponent.h"

#include <cmath>
#include <utility>

#include "model/AutomationCurve.h"

#include "io/SamplePool.h"

#include "model/ChannelColour.h"
#include "model/Ids.h"
#include "model/Meter.h"
#include "model/ProjectEdits.h"
#include "ui/TimelineRuler.h"
#include "ui/Gestures.h"
#include "ui/Hotkeys.h"
#include "ui/HeaderRow.h"
#include "ui/MenuSeam.h"
#include "ui/TimelinePaint.h"
#include "ui/design/Tokens.h"
#include "ui/primitives/DewControls.h"

namespace dew
{

using namespace tokens;

/** One playlist track's header: name, mute and solo.

    Mute and solo are real controls rather than painted text, because the engine
    already honours the properties and there was no way to reach them.
*/
class PlaylistComponent::TrackHeader : public HeaderRow
{
public:
    TrackHeader (ProjectDocument& d, juce::ValueTree t)
        : document (d), track (std::move (t))
    {
        setComponentID ("playlistTrackHeader");
        muteButton.setComponentID ("trackMute");
        soloButton.setComponentID ("trackSolo");

        nameLabel.setText (track[ids::name].toString(), juce::dontSendNotification);
        nameLabel.setEditable (false, true, false);

        // setEditable does not stop a Label eating clicks - it only touches
        // keyboard focus - so without this the label swallows every press across
        // the header's whole left side and the row's own mouseDown never runs.
        // The channel rack learned this the hard way; see the README.
        nameLabel.setInterceptsMouseClicks (false, false);
        nameLabel.setFont (type::font (type::body));
        nameLabel.setColour (juce::Label::textColourId, colour::textPrimary);
        nameLabel.onTextChange = [this]
        {
            ProjectEdits::setProperty (track, ids::name, nameLabel.getText(),
                                       &document.getUndoManager(), "Rename track");
        };
        addAndMakeVisible (nameLabel);

        muteButton.setToggleState ((bool) track[ids::mute], juce::dontSendNotification);
        muteButton.onClick = [this]
        {
            ProjectEdits::setProperty (track, ids::mute, muteButton.getToggleState(),
                                       &document.getUndoManager(), "Mute track");
        };
        addAndMakeVisible (muteButton);

        soloButton.setToggleState ((bool) track[ids::solo], juce::dontSendNotification);
        soloButton.onClick = [this]
        {
            ProjectEdits::setProperty (track, ids::solo, soloButton.getToggleState(),
                                       &document.getUndoManager(), "Solo track");
        };
        addAndMakeVisible (soloButton);
    }

    enum class MenuItem { rename = 1, addTrack, removeTrack };

    juce::PopupMenu buildMenu() const override
    {
        juce::PopupMenu menu;
        menu.addItem ((int) MenuItem::rename, "Rename");
        menu.addItem ((int) MenuItem::addTrack, "Add track");
        menu.addSeparator();
        menu.addItem ((int) MenuItem::removeTrack, "Remove track");
        return menu;
    }

    void applyMenuChoice (int choice) override
    {
        switch ((MenuItem) choice)
        {
            case MenuItem::rename:      nameLabel.showEditor(); break;
            case MenuItem::addTrack:    if (onAddTrack) onAddTrack(); break;
            case MenuItem::removeTrack: if (onRemoveTrack) onRemoveTrack (track); break;
            default: break;
        }
    }

    /** Add and remove belong to the playlist, which owns the list of tracks. */
    std::function<void()> onAddTrack;
    std::function<void (juce::ValueTree)> onRemoveTrack;

    juce::Label* editableLabel() override { return &nameLabel; }

    juce::ValueTree getTrack() const { return track; }

    /** The band's width. Named rather than a bare 4 in a fillRect, and matched
        to the tab the channel rack's own rows draw. */
    static constexpr int colourTabWidth = 4;

    void refresh()
    {
        setComponentID ("playlistTrackHeader");
        muteButton.setComponentID ("trackMute");
        soloButton.setComponentID ("trackSolo");

        nameLabel.setText (track[ids::name].toString(), juce::dontSendNotification);
        muteButton.setToggleState ((bool) track[ids::mute], juce::dontSendNotification);
        soloButton.setToggleState ((bool) track[ids::solo], juce::dontSendNotification);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (colour::surface);

        // A muted track's whole header dims, so the state is readable from
        // across the arrangement and not only from the letter.
        if ((bool) track[ids::mute])
        {
            g.setColour (colour::wellDeep.withAlpha (emphasis::subdued));
            g.fillAll();
        }

        // A band down the whole left edge, not a tab beside the name.
        //
        // It is what stops a tall header being a short row with a hole under it,
        // and it costs nothing: a band scales to any height by construction. The
        // colour is taken from the track's POSITION - channelColour exists for
        // painting things with no stored colour of their own - so this needs no
        // schema change, and the channel rack already draws the same edge.
        g.setColour (tokens::colour::channelColour (index));
        g.fillRect (0, 0, colourTabWidth, getHeight());

        g.setColour (colour::divider);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
    }

    /** Which row this is, for the colour band. Re-set by rebuildHeaders, which
        already rebuilds every header whenever the list changes. */
    void setIndex (int newIndex)
    {
        if (std::exchange (index, newIndex) != newIndex)
            repaint();
    }

    void resized() override
    {
        auto area = getLocalBounds().withTrimmedLeft (colourTabWidth);

        // The row keeps its OWN height, at the top. It does not stretch and it
        // does not centre: a track's name labels the lane's first pixel, which
        // is where its clips begin, and a name that drifts to the middle of a
        // 200px header stops pointing at anything. Toggles stretched to 200px
        // are also not toggles.
        auto row = area.removeFromTop (juce::jmin (area.getHeight(), size::rowHeight))
                       .reduced (space::sm, space::xs);

        // Past the roomy threshold the name gets a line of its own and the
        // toggles drop below it. One row of controls with a void under it is
        // what a tall header looks like otherwise, and the name is the thing
        // there is finally room to read.
        const auto roomy = getHeight() >= size::trackHeightRoomy;

        auto toggles = roomy ? area.removeFromTop (juce::jmin (area.getHeight(), size::rowHeight))
                                   .reduced (space::sm, space::xs)
                             : row;

        soloButton.setBounds (toggles.removeFromRight (size::letterToggle).reduced (0, space::xxs));
        toggles.removeFromRight (space::xxs);
        muteButton.setBounds (toggles.removeFromRight (size::letterToggle).reduced (0, space::xxs));
        toggles.removeFromRight (space::sm);

        nameLabel.setBounds (roomy ? row : toggles);
    }

private:
    ProjectDocument& document;
    juce::ValueTree track;
    int index = 0;

    juce::Label nameLabel;
    DewLetterToggle muteButton { "M", colour::warning, "Mute this track" };
    DewLetterToggle soloButton { "S", colour::accent, "Solo this track" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackHeader)
};

// -----------------------------------------------------------------------------

PlaylistComponent::PlaylistComponent (ProjectDocument& d, AudioEngine& e, EditorState& s, SamplePool* p)
    : document (d), engine (e), editorState (s), samplePool (p)
{
    setComponentID ("playlist");
    document.getState().addListener (this);
    editorState.addChangeListener (this);

    horizontalScroll.addListener (this);
    addChildComponent (horizontalScroll);

    verticalScroll.addListener (this);
    addChildComponent (verticalScroll);

    // Transparent to the pointer itself so the gutter's own handling is
    // unchanged, but its children still take their clicks.
    headerHolder.setInterceptsMouseClicks (false, true);
    addAndMakeVisible (headerHolder);

    addAutomationButton.onClick = [this] { showAutomationMenu(); };
    addAndMakeVisible (addAutomationButton);

    addTrackButton.onClick = [this] { addTrack(); };
    addTrackButton.setComponentID ("addTrackButton");
    headerHolder.addAndMakeVisible (addTrackButton);

    toolbar.onToolChanged = [this] { repaint(); };
    toolbar.onZoom = [this] (double factor)
    {
        // Zero means "fit the song", the same shape the piano roll's strip
        // reports so the two mean one thing.
        if (juce::exactlyEqual (factor, 0.0))
            zoomToFit();
        else
            zoomBy (factor, contentWidth() * 0.5f);
    };

    toolbar.onTrackHeight = [this] (double factor) { zoomTracksBy (factor); };
    addAndMakeVisible (toolbar);

    setWantsKeyboardFocus (true);

    // The ruler works in BARS here, so everything the gesture is handed is in
    // bars and only the seek converts - one place, rather than a conversion in
    // each of the three branches this used to have.
    rulerGesture.unitForX = [this] (int x)
    {
        return juce::jlimit (0.0, (double) numBars(),
                             timeline.stepForX ((float) (x - size::gutterTrack)));
    };

    rulerGesture.context = [this]
    {
        const auto stepsPerBar = Meter::of (document.getState()).stepsPerBar();

        ruler::GestureContext ctx;
        ctx.snapUnits = 1;
        ctx.totalUnits = numBars();
        ctx.playheadUnits = juce::jmax (0.0, engine.getPlayheadSteps() / (double) stepsPerBar);

        return ctx;
    };

    rulerGesture.onSeek = [this] (double bars)
    {
        const auto stepsPerBar = Meter::of (document.getState()).stepsPerBar();

        engine.setPlayheadSteps (bars * (double) stepsPerBar);
        repaint();
    };

    rulerGesture.onRangeChanged = [this] (juce::Range<int> bars)
    {
        editorState.setSelectedBarRange (bars);
        repaint();
    };

    rulerGesture.onRangeCleared = [this]
    {
        editorState.clearBarSelection();
        repaint();
    };

    rebuildHeaders();
    startTimerHz (motion::playheadHz);
}

PlaylistComponent::~PlaylistComponent()
{
    document.getState().removeListener (this);
    editorState.removeChangeListener (this);
}

void PlaylistComponent::refresh()
{
    document.getState().addListener (this);
    rebuildHeaders();
    updateScrollBar();
    repaint();
}

// --- model -------------------------------------------------------------------

juce::ValueTree PlaylistComponent::playlist() const
{
    return document.getState().getChildWithName (ids::PLAYLIST);
}

int PlaylistComponent::numBars() const
{
    return juce::jmax (4, (int) document.getState()[ids::barsInSong]);
}

int PlaylistComponent::getNumTracks() const
{
    int count = 0;

    for (const auto& track : playlist())
        if (track.hasType (ids::PLAYLIST_TRACK))
            ++count;

    return count;
}

juce::ValueTree PlaylistComponent::trackAt (int index) const
{
    int i = 0;

    for (const auto& track : playlist())
        if (track.hasType (ids::PLAYLIST_TRACK) && i++ == index)
            return track;

    return {};
}

// --- geometry ----------------------------------------------------------------

void PlaylistComponent::scrollTracksTo (double offsetPx)
{
    trackScrollPx = juce::jmax (0.0, offsetPx);
    viewIsUsers = true;

    // updateScrollBar re-clamps against the content height, so an offset past
    // the end lands on the end rather than off it.
    updateScrollBar();
    resized();
    repaint();
}

void PlaylistComponent::setTrackHeight (int wanted)
{
    const auto clamped = juce::jlimit (size::trackHeightMin, size::trackHeightMax, wanted);

    if (clamped == trackHeight)
        return;

    // Anchor on the middle of the lane view, for the same reason
    // TimelineView::zoomAround anchors on the pointer: growing the rows from the
    // top walks the arrangement out from under whatever you were looking at.
    const auto anchorLane = (trackScrollPx + (double) laneViewHeight() * 0.5) / (double) trackHeight;

    trackHeight = clamped;
    trackScrollPx = juce::jmax (0.0, anchorLane * (double) trackHeight
                                         - (double) laneViewHeight() * 0.5);

    // A height change is the user taking the view, exactly as a zoom is -
    // otherwise the next resize would re-fit and undo it.
    viewIsUsers = true;

    updateScrollBar();
    resized();
    repaint();
}

void PlaylistComponent::zoomTracksBy (double factor)
{
    if (factor <= 0.0)
    {
        fitTracksToWindow();
        return;
    }

    setTrackHeight ((int) std::lround ((double) trackHeight * factor));
}

void PlaylistComponent::fitTracksToWindow()
{
    // The add-track row counts: fitting to the tracks alone would push the
    // button that adds the next one just off the bottom.
    const auto rows = juce::jmax (1, getNumTracks() + 1);

    setTrackHeight (laneViewHeight() / rows);
}

int PlaylistComponent::tracksBottom() const
{
    return (int) laneY (getNumTracks());
}

int PlaylistComponent::lanesBottom() const
{
    // The jmin, not viewBottom(): with the tracks fitting, this is exactly where
    // the last one ends, which is where the grid and the playhead have always
    // stopped. Taking the view's bottom instead would extend every one of them
    // over empty space the moment a lane could be scrolled.
    return juce::jmin (tracksBottom(), viewBottom());
}

juce::Rectangle<int> PlaylistComponent::getLaneArea() const
{
    return { size::gutterTrack, lanesTop(), (int) contentWidth(), laneViewHeight() };
}

float PlaylistComponent::contentWidth() const
{
    // The vertical scrollbar's strip is reserved whether or not the bar is
    // showing, the way the piano roll's note area reserves its own. Giving it
    // back when the tracks happen to fit would re-lay the arrangement
    // HORIZONTALLY every time a track was added.
    return (float) juce::jmax (0, getWidth() - size::gutterTrack - size::scrollThickness);
}

int PlaylistComponent::barAtX (int x) const
{
    // Not clamped to the song length: dropping a clip past the end is how an
    // arrangement gets longer. The bars beyond are painted inert but stay
    // writable, exactly as the piano roll treats the end of a pattern.
    return juce::jmax (0, timeline.stepAtX ((float) (x - size::gutterTrack)));
}

int PlaylistComponent::trackAtY (int y) const
{
    // std::floor, not integer division: division truncates toward zero, so a y
    // one pixel above the lanes reported track 0 rather than -1. Every caller
    // guards on y < lanesTop() first, which is why it never showed - keep the
    // guards, and do the arithmetic correctly anyway.
    return (int) std::floor (((double) (y - lanesTop()) + trackScrollPx) / (double) trackHeight);
}

juce::Rectangle<float> PlaylistComponent::boundsForClip (const juce::ValueTree& clip, int trackIndex) const
{
    const auto start = (int) clip[ids::startBar];
    const auto length = juce::jmax (1, (int) clip[ids::lengthBars]);

    return { (float) size::gutterTrack + timeline.xForStep ((double) start),
             laneY (trackIndex),
             (float) (length * timeline.pixelsPerStep),
             (float) trackHeight };
}

bool PlaylistComponent::isOnRightEdge (const juce::ValueTree& clip, int trackIndex,
                                       juce::Point<int> position) const
{
    const auto bounds = boundsForClip (clip, trackIndex);
    const auto edge = juce::jmin (10.0f, bounds.getWidth() * 0.3f);

    return (float) position.x >= bounds.getRight() - edge;
}

float PlaylistComponent::playheadX() const
{
    const auto stepsPerBar = Meter::of (document.getState()).stepsPerBar();
    const auto position = engine.getPlayheadSteps() / (double) stepsPerBar;

    return (float) size::gutterTrack + timeline.xForStep (position);
}

// --- layout ------------------------------------------------------------------

void PlaylistComponent::zoomToFit()
{
    if (getWidth() <= 0)
        return;

    // Framing on request still counts as taking the view: it is a zoom someone
    // asked for at a size they can see, not the default one.
    viewIsUsers = true;
    timeline.fit (numBars(), contentWidth());
    updateScrollBar();
    repaint();
}

void PlaylistComponent::zoomBy (double factor, float anchorX)
{
    viewIsUsers = true;
    timeline.zoomAround (factor, anchorX);
    updateScrollBar();
    repaint();
}

void PlaylistComponent::updateScrollBar()
{
    if (getWidth() <= 0)
        return;

    // The scroll range reaches a screen PAST the song, which is what
    // TimelineView::clampScroll already allows and what the piano roll has
    // always done. Pinning it to the song's length is what made the empty
    // space beyond the last bar unreachable - there was nowhere to scroll to.
    const auto visible = timeline.visibleSteps (contentWidth());
    const auto scrollable = visible < (double) numBars() - 1e-9;

    horizontalScroll.setVisible (scrollable);

    if (! scrollable)
        timeline.scrollOffsetSteps = 0.0;

    timeline.clampScroll (contentWidth(), numBars());

    // The content is the tracks PLUS the add-track row: fitting or scrolling to
    // a bottom that hid the button that adds the next track would be the same
    // as not having one.
    const auto contentHeight = (double) ((getNumTracks() + 1) * trackHeight);
    const auto laneView = (double) laneViewHeight();
    const auto overflows = contentHeight > laneView + 1e-9;

    verticalScroll.setVisible (overflows);

    trackScrollPx = overflows ? juce::jlimit (0.0, contentHeight - laneView, trackScrollPx) : 0.0;

    const juce::ScopedValueSetter<bool> quiet (updatingScrollBar, true);
    horizontalScroll.setRangeLimits (0.0, (double) numBars(), juce::dontSendNotification);
    horizontalScroll.setCurrentRange (timeline.scrollOffsetSteps, visible,
                                      juce::dontSendNotification);
    verticalScroll.setRangeLimits (0.0, contentHeight, juce::dontSendNotification);
    verticalScroll.setCurrentRange (trackScrollPx, laneView, juce::dontSendNotification);
}

void PlaylistComponent::rebuildHeaders()
{
    headers.clear();

    for (const auto& track : playlist())
        if (track.hasType (ids::PLAYLIST_TRACK))
        {
            auto* header = headers.add (new TrackHeader (document, track));
            header->onAddTrack = [this] { addTrack(); };
            header->onRemoveTrack = [this] (juce::ValueTree t) { removeTrack (t); };
            header->setIndex (headers.size() - 1);
            headerHolder.addAndMakeVisible (header);
        }

    resized();
}

void PlaylistComponent::resized()
{
    toolbar.setBounds (0, 0, getWidth(), size::stripToolbar);

    horizontalScroll.setBounds (size::gutterTrack, getHeight() - size::scrollThickness,
                                (int) contentWidth(), size::scrollThickness);

    verticalScroll.setBounds (getWidth() - size::scrollThickness, lanesTop(),
                              size::scrollThickness, laneViewHeight());

    // In the corner above the track headers, where the ruler does not reach.
    addAutomationButton.setBounds (juce::Rectangle<int> (0, rulerTop(), size::gutterTrack, size::rulerHeight)
                                       .reduced (space::xs, space::xxs));

    // The holder spans the lane strip; its children sit in ITS coordinates,
    // which is laneY minus the strip's own top.
    headerHolder.setBounds (0, lanesTop(), size::gutterTrack, laneViewHeight());

    const auto rowTop = [this] (int index) { return (int) laneY (index) - lanesTop(); };

    for (int i = 0; i < headers.size(); ++i)
        headers[i]->setBounds (0, rowTop (i), size::gutterTrack, trackHeight);

    // Directly below the last track: the next empty row of the list, where the
    // track it adds will appear. Always present now rather than hidden when the
    // window ran out - it is reachable by scrolling, and an add button that
    // disappears once you have enough tracks is the bug the channel rack already
    // had and fixed.
    addTrackButton.setBounds (juce::Rectangle<int> (0, rowTop (headers.size()),
                                                    size::gutterTrack, trackHeight)
                                  .reduced (space::sm, space::xs));

    // Until someone has zoomed or scrolled, a layout frames the whole song.
    // Latching after the FIRST layout instead would hand the zoom to whatever
    // size the component happened to be built at, which is not the size it ends
    // up; re-fitting always is what made the playlist unzoomable.
    if (! viewIsUsers && getWidth() > 0)
        timeline.fit (numBars(), contentWidth());

    updateScrollBar();
}

void PlaylistComponent::scrollBarMoved (juce::ScrollBar* bar, double start)
{
    if (updatingScrollBar)
        return;

    viewIsUsers = true;

    if (bar == &verticalScroll)
    {
        // The headers are laid out from laneY, so they follow the offset only
        // when something re-lays them - resized() rather than a repaint.
        scrollTracksTo (start);
        return;
    }

    timeline.scrollOffsetSteps = start;
    repaint();
}

void PlaylistComponent::mouseWheelMove (const juce::MouseEvent& event,
                                        const juce::MouseWheelDetails& wheel)
{
    // Mod-wheel zooms around the pointer, exactly as it does over the piano
    // roll. The playlist answered only to scrolling, and only when there was
    // something to scroll.
    const auto delta = gesture::deltaOf (wheel);

    if (gesture::isZoom (event.mods))
    {
        zoomBy (std::pow (2.0, delta.y * gesture::wheelZoomExponent),
                (float) (event.x - size::gutterTrack));
        return;
    }

    viewIsUsers = true;

    // The piano roll's convention, adopted here because the playlist now has
    // two axes to scroll and had only ever had one: shift scrolls time, and a
    // plain wheel scrolls the TRACKS.
    //
    // This changes a shipped gesture - a plain wheel used to scroll time - and
    // it is the change a user notices first. It is still the right one: the two
    // timeline views disagreed about what a plain wheel meant, and the axis the
    // wheel naturally maps to is the one that now moves.
    if (event.mods.isShiftDown())
    {
        timeline.scrollOffsetSteps -= delta.along() * gesture::wheelStepsPerNotch;
        updateScrollBar();
        repaint();
        return;
    }

    timeline.scrollOffsetSteps -= delta.x * gesture::wheelStepsPerNotch;
    scrollTracksTo (trackScrollPx - delta.y * (double) trackHeight);
}

void PlaylistComponent::mouseMagnify (const juce::MouseEvent& event, float scaleFactor)
{
    zoomBy ((double) scaleFactor, (float) (event.x - size::gutterTrack));
}

bool PlaylistComponent::keyPressed (const juce::KeyPress& key)
{
    // One map across the timeline views. The playlist bound four keys and the
    // piano roll bound six of the same ones differently; zoom-to-fit and clear
    // arrive here for the first time because they are in the map, not because
    // anyone remembered to add them twice.
    switch (hotkeys::viewCommandFor (key))
    {
        case hotkeys::ViewCommand::zoomIn:
            zoomBy (1.5, contentWidth() * 0.5f);
            return true;

        case hotkeys::ViewCommand::zoomOut:
            zoomBy (1.0 / 1.5, contentWidth() * 0.5f);
            return true;

        case hotkeys::ViewCommand::zoomToFit:
            zoomToFit();
            return true;

        case hotkeys::ViewCommand::selectTool:
            toolbar.setTool (PlaylistTool::select);
            return true;

        case hotkeys::ViewCommand::paintTool:
            toolbar.setTool (PlaylistTool::paint);
            return true;

        case hotkeys::ViewCommand::clearSelection:
            editorState.clearBarSelection();
            repaint();
            return true;

        // The playlist has no erase tool, no note selection to delete and no
        // select-all: a clip is deleted through its own menu. Listed rather
        // than defaulted so adding a command to the map is a compile error
        // here until this view says what it does about it.
        case hotkeys::ViewCommand::eraseTool:
        case hotkeys::ViewCommand::deleteSelection:
        case hotkeys::ViewCommand::selectAll:
        case hotkeys::ViewCommand::none:
            break;
    }

    return false;
}

// --- gestures ----------------------------------------------------------------

void PlaylistComponent::mouseMove (const juce::MouseEvent& event)
{
    const auto trackIndex = trackAtY (event.y);
    const auto track = trackAt (trackIndex);
    const auto clip = track.isValid() ? ProjectEdits::findClipAtBar (track, barAtX (event.x))
                                      : juce::ValueTree();

    setMouseCursor (clip.isValid() && isOnRightEdge (clip, trackIndex, event.getPosition())
                        ? juce::MouseCursor::LeftRightResizeCursor
                        : juce::MouseCursor::NormalCursor);
}

void PlaylistComponent::openPatternOf (const juce::ValueTree& clip)
{
    if (! clip.isValid())
        return;

    // Only a MIDI clip has a pattern. Every clip carries a patternId - the
    // schema keeps all three references on one node - so without this an
    // automation or audio clip would open whatever pattern its unused,
    // defaulted id happened to name.
    if (! ProjectEdits::isMidiClip (clip))
        return;

    const auto patternId = (int) clip[ids::patternId];

    if (! ProjectEdits::findPattern (document.getState(), patternId).isValid())
        return;

    editorState.setCurrentPatternId (patternId);
    engine.setCurrentPatternId (patternId);

    if (onOpenPatternInPianoRoll != nullptr)
        onOpenPatternInPianoRoll();
}

juce::ValueTree PlaylistComponent::automationOf (const juce::ValueTree& clip) const
{
    if (! ProjectEdits::isAutomationClip (clip))
        return {};

    return ProjectEdits::findAutomation (document.getState(), (int) clip[ids::automationId]);
}

juce::Point<float> PlaylistComponent::pointPosition (const juce::ValueTree& clip, int trackIndex,
                                                     const juce::ValueTree& point) const
{
    const auto bounds = boundsForClip (clip, trackIndex).reduced (2.0f, 3.0f);
    const auto stepsPerBar = Meter::of (document.getState()).stepsPerBar();
    const auto clipSteps = juce::jmax (1, (int) clip[ids::lengthBars] * stepsPerBar);

    const auto t = juce::jlimit (0.0, 1.0, (double) point[ids::step] / (double) clipSteps);
    const auto value = juce::jlimit (0.0, 1.0, (double) point[ids::value]);

    return { bounds.getX() + (float) t * bounds.getWidth(),
             bounds.getBottom() - (float) value * bounds.getHeight() };
}

void PlaylistComponent::positionToCurve (const juce::ValueTree& clip, int trackIndex,
                                         juce::Point<int> position, double& step, double& value) const
{
    const auto bounds = boundsForClip (clip, trackIndex).reduced (2.0f, 3.0f);
    const auto stepsPerBar = Meter::of (document.getState()).stepsPerBar();
    const auto clipSteps = juce::jmax (1, (int) clip[ids::lengthBars] * stepsPerBar);

    const auto t = bounds.getWidth() > 0.0f
                     ? juce::jlimit (0.0, 1.0, (double) (position.x - bounds.getX()) / bounds.getWidth())
                     : 0.0;

    step = t * clipSteps;
    value = bounds.getHeight() > 0.0f
              ? juce::jlimit (0.0, 1.0, (double) (bounds.getBottom() - position.y) / bounds.getHeight())
              : 0.0;
}

juce::ValueTree PlaylistComponent::pointAt (const juce::ValueTree& clip, int trackIndex,
                                            juce::Point<int> position) const
{
    const auto automation = automationOf (clip);

    if (! automation.isValid())
        return {};

    juce::ValueTree closest;
    auto closestDistance = pointGrabRadius;

    for (const auto& point : automation)
    {
        if (! point.hasType (ids::POINT))
            continue;

        const auto distance = pointPosition (clip, trackIndex, point).getDistanceFrom (position.toFloat());

        if (distance <= closestDistance)
        {
            closestDistance = distance;
            closest = point;
        }
    }

    return closest;
}

juce::ValueTree PlaylistComponent::createAutomationClip (const AutomationTarget& target,
                                                         int startBar, int lengthBars)
{
    auto& undo = document.getUndoManager();
    undo.beginNewTransaction ("Add automation");

    auto automation = ProjectEdits::addAutomation (document.getState(), target, &undo);

    if (! automation.isValid())
        return {};

    // Onto the first lane with room at that bar, so a new automation is visible
    // rather than stacked invisibly under a pattern clip.
    for (int i = 0; i < getNumTracks(); ++i)
    {
        auto track = trackAt (i);

        if (ProjectEdits::findClipAtBar (track, startBar).isValid())
            continue;

        auto clip = ProjectEdits::addAutomationClip (track, (int) automation[ids::id],
                                                     startBar, lengthBars, &undo);
        ProjectEdits::growSongToFitClips (document.getState(), &undo);
        updateScrollBar();
        repaint();
        return clip;
    }

    // Every lane is occupied there: undo the definition rather than leaving one
    // behind that nothing refers to.
    ProjectEdits::removeAutomation (document.getState(), automation, &undo);
    return {};
}

namespace
{
    // Numbered EXPLICITLY. These ids are what applyClipMenuChoice takes, so a
    // test names an item by its number - and inserting an item in the middle
    // would silently re-aim every one of them at something else.
    enum class ClipMenuItem
    {
        openPattern      = 1,
        deleteClip       = 2,
        deletePoint      = 3,
        addClip          = 4,
        duplicatePattern = 5
    };
}

juce::PopupMenu PlaylistComponent::buildClipMenu (const juce::ValueTree& track, int bar) const
{
    juce::PopupMenu menu;
    const auto clip = ProjectEdits::findClipAtBar (track, bar);

    if (menuPoint.isValid())
    {
        menu.addItem ((int) ClipMenuItem::deletePoint, "Delete point");
        return menu;
    }

    if (! clip.isValid())
    {
        menu.addItem ((int) ClipMenuItem::addClip, "Add clip here");
        return menu;
    }

    // Only a MIDI clip has a pattern to open, so offering it anywhere else
    // would be an item that does nothing on some of the clips in the
    // arrangement.
    if (ProjectEdits::isMidiClip (clip))
    {
        menu.addItem ((int) ClipMenuItem::openPattern, "Open pattern");

        // Gives THIS clip a pattern of its own. A pattern is shared by every
        // clip that names it, so the only way to vary one repeat of a phrase
        // was to make a pattern in the transport bar and re-point the clip by
        // hand.
        menu.addItem ((int) ClipMenuItem::duplicatePattern, "Duplicate pattern");
    }

    if (menu.getNumItems() > 0)
        menu.addSeparator();

    menu.addItem ((int) ClipMenuItem::deleteClip, "Delete clip");
    return menu;
}

void PlaylistComponent::applyClipChoice (juce::ValueTree track, int bar, int choice)
{
    auto clip = ProjectEdits::findClipAtBar (track, bar);
    auto& undo = document.getUndoManager();

    switch ((ClipMenuItem) choice)
    {
        case ClipMenuItem::openPattern:
            openPatternOf (clip);
            break;

        case ClipMenuItem::duplicatePattern:
            if (clip.isValid() && ProjectEdits::isMidiClip (clip))
            {
                auto pattern = ProjectEdits::findPattern (document.getState(),
                                                          (int) clip[ids::patternId]);

                if (pattern.isValid())
                {
                    undo.beginNewTransaction ("Duplicate pattern");

                    // One transaction covers both halves: a copy nothing points
                    // at, or a clip pointing at a pattern that undo took away,
                    // are each worse than the state this started in.
                    if (auto fresh = ProjectEdits::duplicatePattern (document.getState(),
                                                                     pattern, &undo);
                        fresh.isValid())
                        ProjectEdits::setProperty (clip, ids::patternId, (int) fresh[ids::id],
                                                   &undo, "Duplicate pattern", true);
                }
            }
            break;

        case ClipMenuItem::deleteClip:
            if (clip.isValid())
            {
                undo.beginNewTransaction ("Delete clip");
                ProjectEdits::removeClip (track, clip, &undo);
            }
            break;

        case ClipMenuItem::deletePoint:
            if (menuPoint.isValid())
            {
                undo.beginNewTransaction ("Remove automation point");
                ProjectEdits::removeAutomationPoint (automationOf (clip), menuPoint, &undo);
            }
            break;

        case ClipMenuItem::addClip:
            undo.beginNewTransaction ("Add clip");
            ProjectEdits::addClip (track, editorState.getCurrentPatternId(), bar, 1, &undo);
            ProjectEdits::growSongToFitClips (document.getState(), &undo);
            updateScrollBar();
            break;

        default:
            break;
    }

    menuPoint = {};
    repaint();
}

void PlaylistComponent::addTrack()
{
    auto& undo = document.getUndoManager();
    undo.beginNewTransaction ("Add track");
    ProjectEdits::addPlaylistTrack (document.getState(), {}, &undo);
}

void PlaylistComponent::removeTrack (juce::ValueTree track)
{
    if (! track.isValid())
        return;

    auto& undo = document.getUndoManager();
    undo.beginNewTransaction ("Remove track");
    ProjectEdits::removePlaylistTrack (document.getState(), track, &undo);
}

bool PlaylistComponent::applyTrackMenuChoice (int trackIndex, int choice)
{
    if (! juce::isPositiveAndBelow (trackIndex, headers.size()))
        return false;

    headers[trackIndex]->applyMenuChoice (choice);
    return true;
}

juce::StringArray PlaylistComponent::trackMenuItems (int trackIndex) const
{
    if (! juce::isPositiveAndBelow (trackIndex, headers.size()))
        return {};

    const auto menu = headers[trackIndex]->buildMenu();

    return menuItems (menu);
}

juce::StringArray PlaylistComponent::clipMenuItems (int trackIndex, int bar) const
{
    const auto menu = buildClipMenu (trackAt (trackIndex), bar);

    return menuItems (menu);
}

bool PlaylistComponent::applyClipMenuChoice (int trackIndex, int bar, int choice)
{
    auto track = trackAt (trackIndex);

    if (! track.isValid())
        return false;

    applyClipChoice (track, bar, choice);
    return true;
}

void PlaylistComponent::showAutomationMenu()
{
    const auto targets = availableAutomationTargets (document.getState());

    juce::PopupMenu menu;
    juce::PopupMenu submenu;
    juce::String currentGroup;
    int itemId = 1;

    // Grouped by what they belong to: a flat list of every parameter of every
    // effect on every channel is unreadable by the third channel.
    for (const auto& target : targets)
    {
        const auto group = target.displayName.upToFirstOccurrenceOf (" > ", false, false);

        if (group != currentGroup)
        {
            if (currentGroup.isNotEmpty())
                menu.addSubMenu (currentGroup, submenu);

            submenu.clear();
            currentGroup = group;
        }

        submenu.addItem (itemId++, target.displayName.fromFirstOccurrenceOf (" > ", false, false));
    }

    if (currentGroup.isNotEmpty())
        menu.addSubMenu (currentGroup, submenu);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addAutomationButton),
                        [this, targets] (int choice)
                        {
                            if (choice > 0 && choice <= (int) targets.size())
                                createAutomationClip (targets[(size_t) choice - 1], 0, 4);
                        });
}

void PlaylistComponent::mouseDoubleClick (const juce::MouseEvent& event)
{
    // On the ruler, a double-click clears the span - one rule, shared with the
    // piano roll and the channel rack rather than repeated in each of them.
    if (event.x >= size::gutterTrack && event.y >= rulerTop() && event.y < lanesTop())
    {
        rulerGesture.mouseDoubleClick (event);
        return;
    }

    if (event.x < size::gutterTrack || event.y < lanesTop())
        return;

    const auto track = trackAt (trackAtY (event.y));

    if (! track.isValid())
        return;

    const auto clip = ProjectEdits::findClipAtBar (track, barAtX (event.x));

    if (auto automation = automationOf (clip); automation.isValid())
    {
        // Double-click inside a curve adds a point there. A single click has to
        // stay "move the clip", or an automation clip could not be moved.
        double step = 0.0, value = 0.0;
        positionToCurve (clip, trackAtY (event.y), event.getPosition(), step, value);

        auto& undo = document.getUndoManager();
        undo.beginNewTransaction ("Add automation point");
        ProjectEdits::addAutomationPoint (automation, step, value, &undo);
        repaint();
        return;
    }

    // A pattern clip is a reference to a pattern, so the obvious thing to do
    // with one is to go and edit that pattern. Previously the only route was
    // the pattern selector in the transport bar.
    openPatternOf (clip);
}

void PlaylistComponent::mouseDown (const juce::MouseEvent& event)
{
    // It asks for focus in its constructor but never took it, so its keys only
    // worked if something else had happened to hand it over.
    grabKeyboardFocus();

    // The ruler used to be excluded outright by this guard, so the playlist's
    // was as inert as the piano roll's. Everything it does now lives in
    // ruler::Gesture, which is why this is one line rather than three branches
    // the piano roll also had a copy of.
    if (event.x >= size::gutterTrack && event.y >= rulerTop() && event.y < lanesTop())
    {
        rulerGesture.mouseDown (event);
        return;
    }

    if (event.x < size::gutterTrack || event.y < lanesTop() || event.y >= lanesBottom())
        return;

    const auto trackIndex = trackAtY (event.y);
    auto track = trackAt (trackIndex);

    if (! track.isValid())
        return;

    const auto bar = barAtX (event.x);
    auto clip = ProjectEdits::findClipAtBar (track, bar);
    auto& undo = document.getUndoManager();

    // Alt-click still removes outright: it is the sweep-to-clear gesture the
    // piano roll and step grid share, and losing it would cost a habit to gain a
    // menu that is already on the other button.
    if (event.mods.isAltDown())
    {
        // On a point, remove the point; anywhere else, remove the clip. Without
        // this a curve could gain points but never lose one.
        if (auto point = pointAt (clip, trackIndex, event.getPosition()); point.isValid())
        {
            undo.beginNewTransaction ("Remove automation point");
            ProjectEdits::removeAutomationPoint (automationOf (clip), point, &undo);
            repaint();
            return;
        }

        if (clip.isValid())
        {
            undo.beginNewTransaction ("Delete clip");
            ProjectEdits::removeClip (track, clip, &undo);
            repaint();
        }
        return;
    }

    // Right-click opens a menu instead, so deleting a clip is a thing you choose
    // rather than a thing that happens on the way past.
    if (event.mods.isPopupMenu())
    {
        menuPoint = pointAt (clip, trackIndex, event.getPosition());

        auto menu = buildClipMenu (track, bar);
        menu.setLookAndFeel (&getLookAndFeel());
        menu.showMenuAsync (juce::PopupMenu::Options()
                                .withTargetScreenArea ({ event.getScreenX(), event.getScreenY(), 1, 1 }),
                            [safe = juce::Component::SafePointer<PlaylistComponent> (this), track, bar] (int choice)
                            {
                                if (safe != nullptr && choice > 0)
                                    safe->applyClipChoice (track, bar, choice);
                            });
        return;
    }

    if (auto point = pointAt (clip, trackIndex, event.getPosition()); point.isValid())
    {
        draggedClip = clip;
        draggedClipTrack = track;
        draggedPoint = point;
        dropTrackIndex = trackIndex;
        gesture = Gesture::draggingPoint;
        undo.beginNewTransaction ("Move automation point");
        return;
    }

    if (clip.isValid())
    {
        draggedClip = clip;
        draggedClipTrack = track;
        dropTrackIndex = trackIndex;

        if (isOnRightEdge (clip, trackIndex, event.getPosition()))
        {
            gesture = Gesture::resizing;
            undo.beginNewTransaction ("Resize clip");
        }
        else
        {
            // Mod copies rather than moves, and mod-shift gives the copy a
            // pattern of its own. Latched here rather than read on every drag
            // sample: a modifier let go halfway through a drag must not turn a
            // copy back into a move, and the copy has to be made exactly once.
            dragCopies = event.mods.isCommandDown() || event.mods.isCtrlDown();
            dragCopyIsUnique = dragCopies && event.mods.isShiftDown();

            gesture = Gesture::moving;
            dragBarOffset = bar - (int) clip[ids::startBar];
            undo.beginNewTransaction (dragCopies ? "Copy clip" : "Move clip");
        }

        return;
    }

    // The paint tool lays a clip per cell the pointer crosses. Placing one and
    // sizing it with the same drag is the select tool's gesture and is not
    // duplicated here.
    if (toolbar.getTool() == PlaylistTool::paint)
    {
        gesture = Gesture::painting;
        lastPaintedCell = { -1, -1 };
        undo.beginNewTransaction ("Paint clips");

        if (paintClipAt (event.getPosition()))
            repaint();

        return;
    }

    undo.beginNewTransaction ("Add clip");
    draggedClip = ProjectEdits::addClip (track, editorState.getCurrentPatternId(), bar, 1, &undo);
    draggedClipTrack = track;
    dropTrackIndex = trackIndex;
    gesture = Gesture::resizing;
    ProjectEdits::growSongToFitClips (document.getState(), &undo);
    updateScrollBar();
    repaint();
}

bool PlaylistComponent::paintClipAt (juce::Point<int> position)
{
    if (position.x < size::gutterTrack || position.y < lanesTop() || position.y >= lanesBottom())
        return false;

    const auto trackIndex = trackAtY (position.y);
    auto track = trackAt (trackIndex);

    if (! track.isValid())
        return false;

    const auto bar = barAtX (position.x);

    if (bar == lastPaintedCell.x && trackIndex == lastPaintedCell.y)
        return false;

    lastPaintedCell = { bar, trackIndex };

    // Anything already occupying this bar, whether it starts here or runs
    // through it - painting over a clip should not stack a second one inside it.
    if (ProjectEdits::findClipAtBar (track, bar).isValid())
        return false;

    auto& undo = document.getUndoManager();

    ProjectEdits::addClip (track, editorState.getCurrentPatternId(), bar,
                           editorState.getLastClipLengthBars(), &undo);
    ProjectEdits::growSongToFitClips (document.getState(), &undo);
    return true;
}

void PlaylistComponent::beginCopyDrag (int targetTrackIndex, int targetBar)
{
    auto targetTrack = trackAt (targetTrackIndex);

    if (! targetTrack.isValid() || ! draggedClip.isValid())
        return;

    auto& undo = document.getUndoManager();
    auto source = draggedClip;

    // A unique copy gets a pattern of its own, so editing it afterwards does
    // not edit every clip that shared the original.
    if (dragCopyIsUnique && ProjectEdits::isMidiClip (source))
    {
        auto pattern = ProjectEdits::findPattern (document.getState(),
                                                  (int) source[ids::patternId]);

        if (pattern.isValid())
        {
            auto fresh = ProjectEdits::duplicatePattern (document.getState(), pattern, &undo);

            if (fresh.isValid())
            {
                source = source.createCopy();
                source.setProperty (ids::patternId, (int) fresh[ids::id], nullptr);
            }
        }
    }

    // The gesture rebinds to the COPY and leaves the original where it was -
    // the same rebinding a cross-track move already forces, for the same reason:
    // the rest of the drag would otherwise edit a node nothing is looking at.
    if (auto copy = ProjectEdits::copyClip (targetTrack, source, targetBar, &undo);
        copy.isValid())
    {
        draggedClip = copy;
        draggedClipTrack = targetTrack;
        dropTrackIndex = targetTrackIndex;
    }

    dragCopies = false;
    dragCopyIsUnique = false;
}

void PlaylistComponent::mouseDrag (const juce::MouseEvent& event)
{
    if (rulerGesture.mouseDrag (event))
        return;

    if (gesture == Gesture::painting)
    {
        if (paintClipAt (event.getPosition()))
        {
            updateScrollBar();
            repaint();
        }

        return;
    }

    if (! draggedClip.isValid())
        return;

    auto& undo = document.getUndoManager();

    if (gesture == Gesture::draggingPoint)
    {
        double step = 0.0, value = 0.0;
        positionToCurve (draggedClip, dropTrackIndex, event.getPosition(), step, value);
        ProjectEdits::moveAutomationPoint (automationOf (draggedClip), draggedPoint, step, value, &undo);
        repaint();
        return;
    }

    if (gesture == Gesture::resizing)
    {
        const auto length = barAtX (event.x) - (int) draggedClip[ids::startBar] + 1;
        ProjectEdits::resizeClip (draggedClip, juce::jmax (1, length), &undo);
    }
    else if (gesture == Gesture::moving)
    {
        const auto targetBar = juce::jmax (0, barAtX (event.x) - dragBarOffset);
        const auto targetTrackIndex = juce::jlimit (0, juce::jmax (0, getNumTracks() - 1),
                                                    trackAtY (event.y));

        // On the first drag that actually goes somewhere, so a mod-press that
        // never moves does not litter a copy on top of its own original.
        if (dragCopies && (targetBar != (int) draggedClip[ids::startBar]
                           || targetTrackIndex != dropTrackIndex))
            beginCopyDrag (targetTrackIndex, targetBar);

        auto targetTrack = trackAt (targetTrackIndex);

        if (targetTrack.isValid() && targetTrack != draggedClipTrack)
        {
            // Crossing tracks replaces the clip's tree, so the drag has to keep
            // following the new one or the rest of the gesture edits a detached
            // node and nothing appears to happen.
            draggedClip = ProjectEdits::moveClipToTrack (draggedClipTrack, draggedClip,
                                                         targetTrack, targetBar, &undo);
            draggedClipTrack = targetTrack;
            dropTrackIndex = targetTrackIndex;
        }
        else
        {
            ProjectEdits::moveClip (draggedClip, targetBar, &undo);
        }
    }

    ProjectEdits::growSongToFitClips (document.getState(), &undo);
    updateScrollBar();
    repaint();
}

void PlaylistComponent::mouseUp (const juce::MouseEvent& event)
{
    // Falls through rather than returning: a ruler gesture leaves none of the
    // clip-dragging state set, so the reset below is a no-op for it.
    rulerGesture.mouseUp (event);

    // The next clip painted takes the length of the last one sized, so laying a
    // run of four-bar clips does not mean resizing every one.
    if (draggedClip.isValid() && (gesture == Gesture::resizing || gesture == Gesture::moving))
        editorState.rememberClip ((int) draggedClip[ids::lengthBars]);

    draggedClip = {};
    draggedClipTrack = {};
    draggedPoint = {};
    dropTrackIndex = -1;
    gesture = Gesture::none;
    dragCopies = false;
    dragCopyIsUnique = false;
    lastPaintedCell = { -1, -1 };
    repaint();
}

// --- notifications -----------------------------------------------------------

void PlaylistComponent::timerCallback()
{
    // Not gated on isPlaying any more: the indicator stays put when the
    // transport stops, so Stop visibly returns it to the start rather than
    // making it disappear. Only the wrong MODE has nothing to show.
    if (engine.getMode() != Transport::Mode::song)
    {
        if (lastPaintedPlayheadX >= 0.0f)
        {
            lastPaintedPlayheadX = -1.0f;
            repaint();
        }

        return;
    }

    const auto x = playheadX();

    if (std::abs (x - lastPaintedPlayheadX) < 0.5f && lastPlaying == engine.isPlaying())
        return;

    lastPlaying = engine.isPlaying();

    // Repaint the strip the line was in and the one it moved to, rather than the
    // whole arrangement. Repainting on a bar change was what made the playhead
    // jump a bar at a time instead of moving.
    const auto from = juce::jmin (x, lastPaintedPlayheadX < 0.0f ? x : lastPaintedPlayheadX);
    const auto to   = juce::jmax (x, lastPaintedPlayheadX < 0.0f ? x : lastPaintedPlayheadX);

    lastPaintedPlayheadX = x;
    repaint ((int) from - 3, rulerTop(), (int) (to - from) + 7, lanesBottom() - rulerTop());
}

void PlaylistComponent::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    if (property == ids::barsInSong)
        updateScrollBar();

    if (tree.hasType (ids::PLAYLIST_TRACK))
        for (auto* header : headers)
            header->refresh();

    repaint();
}

void PlaylistComponent::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
{
    if (child.hasType (ids::PLAYLIST_TRACK) || parent.hasType (ids::PLAYLIST))
        rebuildHeaders();

    repaint();
}

void PlaylistComponent::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int)
{
    if (child.hasType (ids::PLAYLIST_TRACK) || parent.hasType (ids::PLAYLIST))
        rebuildHeaders();

    repaint();
}

void PlaylistComponent::changeListenerCallback (juce::ChangeBroadcaster*) { repaint(); }

// --- painting ----------------------------------------------------------------

void PlaylistComponent::paintAudioClip (juce::Graphics& g, const juce::ValueTree& clip,
                                        juce::Rectangle<float> bounds, bool audible)
{
    const auto channel = ProjectEdits::findChannel (document.getState(), (int) clip[ids::channelId]);

    // The channel's own colour rather than the accent, because an audio clip IS
    // its channel - one recording, one channel - and the arrangement should say
    // which one at a glance, the way the channel rack's colour tabs do.
    auto clipColour = channel.isValid()
                          ? channelColour::of (channel)
                          : colour::textDisabled;

    if (! audible)
        clipColour = emphasis::silenced (clipColour);

    g.setColour (clipColour.withAlpha (audible ? emphasis::subdued : emphasis::wash));
    g.fillRoundedRectangle (bounds, radius::sm);
    g.setColour (clipColour.brighter (emphasis::edgeLift).withAlpha (audible ? 1.0f : emphasis::dimmed));
    g.drawRoundedRectangle (bounds, radius::sm, stroke::regular);

    auto* pool = samplePool;
    const auto sample = channel.getChildWithName (ids::SAMPLE);
    const auto path = sample.isValid() ? sample[ids::file].toString() : juce::String();

    if (pool != nullptr && path.isNotEmpty())
    {
        const auto& entry = pool->loadReference (path);

        if (entry.isValid())
        {
            // Clipped to the clip, like the automation curve beside it: a
            // recording longer than the bars it was given must not draw over
            // the clip after it.
            const juce::Graphics::ScopedSaveState clipped (g);
            g.reduceClipRegion (bounds.toNearestInt());

            const auto trace = clipColour.brighter (emphasis::edgeLift)
                                  .withAlpha (audible ? emphasis::strong : emphasis::subdued);
            const juce::Range<float> span { bounds.getX(), bounds.getRight() };

            paint::waveform (g, { bounds.getY(), bounds.getBottom() }, span, span,
                             entry.peaks, [trace] (float) { return trace; });
        }
    }

    g.setColour (colour::textPrimary.withAlpha (audible ? emphasis::strong : emphasis::dimmed));
    g.setFont (type::font (type::small, true));
    g.drawText (channel.isValid() ? channel[ids::name].toString()
                                  : "channel " + clip[ids::channelId].toString(),
                bounds.reduced (6.0f, 2.0f).toNearestInt(), juce::Justification::topLeft, false);
}

void PlaylistComponent::paintAutomationClip (juce::Graphics& g, const juce::ValueTree& clip,
                                             int trackIndex, juce::Rectangle<float> bounds,
                                             bool audible)
{
    const auto automation = automationOf (clip);

    // An automation clip reads as a different kind of thing from a pattern
    // clip: no fill, a visible curve, and its own colour.
    const auto clipColour = audible ? colour::warning
                                    : emphasis::silenced (colour::warning);

    g.setColour (colour::wellDeep.withAlpha (emphasis::strong));
    g.fillRoundedRectangle (bounds, radius::sm);
    g.setColour (clipColour.withAlpha (audible ? emphasis::strong : emphasis::subdued));
    g.drawRoundedRectangle (bounds, radius::sm, stroke::regular);

    if (! automation.isValid())
    {
        g.setColour (colour::danger);
        g.setFont (type::font (type::caption));
        g.drawText ("missing automation", bounds.toNearestInt().reduced (space::xs, 0),
                    juce::Justification::centredLeft, true);
        return;
    }

    // Underneath the curve rather than over it: the curve is the content, and
    // an automation lane is only a row tall.
    g.setColour (clipColour.withAlpha (emphasis::subdued));
    g.setFont (type::font (type::caption));
    g.drawText (automation[ids::name].toString(), bounds.toNearestInt().reduced (space::xs, space::xxs),
                juce::Justification::topLeft, true);

    juce::Array<juce::ValueTree> points;

    for (const auto& point : automation)
        if (point.hasType (ids::POINT))
            points.add (point);

    const juce::Graphics::ScopedSaveState clipped (g);
    g.reduceClipRegion (bounds.toNearestInt());

    // SAMPLED from the same evaluator the audio thread uses, one point per pixel
    // column, rather than a straight lineTo between the points.
    //
    // This painter used to draw every segment as a chord, so a bend - which both
    // evaluators have always honoured - was heard and not seen. Approximating
    // the shape with a quadraticTo instead would be a second, different curve:
    // close, visibly wrong at a full bend, and wrong in a way nobody would
    // notice for a year. Sampling has one formula by construction, and a stepped
    // segment gets its square edge for free.
    juce::Path curve;

    if (points.size() > 1)
    {
        const auto model = curvePointsOf (automation);
        const auto stepsPerBar = Meter::of (document.getState()).stepsPerBar();
        const auto clipSteps = (double) juce::jmax (1, (int) clip[ids::lengthBars] * stepsPerBar);

        const auto left = pointPosition (clip, trackIndex, points.getFirst());
        const auto right = pointPosition (clip, trackIndex, points.getLast());
        const auto columns = juce::jmax (2, (int) std::ceil (right.x - left.x));

        curve.startNewSubPath (left);

        // Deliberately NOT reaching t == 1 in the loop: the final lineTo below
        // puts the last point exactly where its handle is drawn, for every
        // shape, which a loop running to the end only approximately does - and
        // it is what makes a stepped segment jump rather than lean.
        for (int i = 1; i < columns; ++i)
        {
            const auto x = left.x + (right.x - left.x) * (float) i / (float) columns;
            const auto step = (double) ((x - bounds.getX()) / bounds.getWidth()) * clipSteps;
            const auto value = curveValueAt (model, step);

            curve.lineTo (x, bounds.getBottom() - (float) value * bounds.getHeight());
        }

        curve.lineTo (right);

        g.setColour (clipColour);
        g.strokePath (curve, juce::PathStrokeType (stroke::regular));
    }

    for (const auto& point : points)
    {
        const auto position = pointPosition (clip, trackIndex, point);
        g.setColour (colour::wellDeep);
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (position));
        g.setColour (clipColour);
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (position));
    }

}

/** The lanes and the clips on them. Its own function because it is the only
    part of paint() that is CLIPPED: a lane can be scrolled now, and without a
    clip region the stripe and the divider of a half-scrolled first track are
    drawn straight over the ruler. Returns how many tracks it walked, which is
    what tells paint() whether to draw the empty state.
*/
int PlaylistComponent::paintLanes (juce::Graphics& g, int bottom, bool anySolo)
{
    int trackIndex = 0;

    const juce::Graphics::ScopedSaveState lanesClipped (g);
    g.reduceClipRegion (getLaneArea());

    for (const auto& track : playlist())
    {
        if (! track.hasType (ids::PLAYLIST_TRACK))
            continue;

        const auto y = (int) laneY (trackIndex);

        // A lane wholly outside the view paints nothing. Twenty tracks at the
        // tallest height would otherwise paint twenty lanes to show three.
        if (y >= viewBottom() || y + trackHeight <= lanesTop())
        {
            ++trackIndex;
            continue;
        }

        const auto audible = ! (bool) track[ids::mute] && (! anySolo || (bool) track[ids::solo]);

        if (trackIndex % 2 == 1)
        {
            g.setColour (colour::wellDeep.withAlpha (emphasis::dimmed));
            g.fillRect (size::gutterTrack, y, getWidth() - size::gutterTrack, trackHeight);
        }

        // The lane a clip is being dragged onto, so a cross-track drop lands
        // where you meant it to.
        if (gesture == Gesture::moving && trackIndex == dropTrackIndex)
        {
            g.setColour (colour::accent.withAlpha (emphasis::tint));
            g.fillRect (size::gutterTrack, y, getWidth() - size::gutterTrack, trackHeight);
        }

        g.setColour (colour::divider);
        g.drawHorizontalLine (y, (float) size::gutterTrack, (float) getWidth());

        for (const auto& clip : track)
        {
            if (! clip.hasType (ids::CLIP))
                continue;

            const auto bounds = boundsForClip (clip, trackIndex).reduced (2.0f, 3.0f);

            if (! bounds.intersects (juce::Rectangle<float> ((float) size::gutterTrack, (float) lanesTop(),
                                                             contentWidth(),
                                                             (float) (bottom - lanesTop()))))
                continue;

            if (ProjectEdits::isAutomationClip (clip))
            {
                paintAutomationClip (g, clip, trackIndex, bounds, audible);
                continue;
            }

            if (ProjectEdits::isAudioClip (clip))
            {
                paintAudioClip (g, clip, bounds, audible);
                continue;
            }

            const auto pattern = ProjectEdits::findPattern (document.getState(),
                                                            (int) clip[ids::patternId]);

            const auto isCurrent = (int) clip[ids::patternId] == editorState.getCurrentPatternId();
            auto clipColour = isCurrent ? colour::accent : emphasis::secondary (colour::accent);

            // A clip on a silenced track is drawn as silenced, so mute and solo
            // are visible in the arrangement and not only in the headers.
            if (! audible)
                clipColour = emphasis::silenced (clipColour);

            g.setColour (clipColour.withAlpha (audible ? emphasis::strong : emphasis::subdued));
            g.fillRoundedRectangle (bounds, radius::sm);
            g.setColour (clipColour.brighter (emphasis::edgeLift).withAlpha (audible ? 1.0f : emphasis::dimmed));
            g.drawRoundedRectangle (bounds, radius::sm, stroke::regular);

            g.setColour (colour::textOnAccent);
            g.setFont (type::font (type::small, true));
            g.drawText (pattern.isValid() ? pattern[ids::name].toString()
                                          : "pattern " + clip[ids::patternId].toString(),
                        bounds.toNearestInt().reduced (space::xs, 0), juce::Justification::centredLeft, true);
        }

        ++trackIndex;
    }

    return trackIndex;
}

void PlaylistComponent::paint (juce::Graphics& g)
{
    const auto bars = numBars();
    const auto width = (float) timeline.pixelsPerStep;
    // Where the grid, the selection band and the playhead stop. lanesBottom
    // rather than tracksBottom, so a scrolled arrangement does not draw its
    // furniture over the strip below the view.
    const auto bottom = lanesBottom();
    // The grid is drawn over the WHOLE width, so bar lines and numbers reach
    // the edge of the window rather than stopping with the arrangement. The
    // same change the two pattern editors got - three views that all stopped
    // mid-panel would have become one that still did.
    const auto painted = timeline.visibleStepRange (contentWidth());
    const auto anySolo = [this]
    {
        for (const auto& track : playlist())
            if (track.hasType (ids::PLAYLIST_TRACK) && (bool) track[ids::solo])
                return true;

        return false;
    }();

    g.fillAll (colour::well);

    // Below the last track is not a lane that stopped working.
    paint::inertArea (g, { 0, bottom, getWidth(), juce::jmax (0, getHeight() - bottom) });


    // --- ruler ---------------------------------------------------------------
    g.setColour (colour::surface);
    g.fillRect (0, rulerTop(), getWidth(), size::rulerHeight);

    // The selection's strip goes down before the bar numbers, so the numbers
    // inside it stay legible instead of being washed out by it.
    if (editorState.hasBarSelection())
    {
        const auto selection = editorState.getSelectedBarRange();

        const auto fromX = (float) size::gutterTrack + timeline.xForStep ((double) selection.getStart());
        const auto toX = (float) size::gutterTrack + timeline.xForStep ((double) selection.getEnd());

        g.setColour (colour::accent.withAlpha (emphasis::dimmed));
        g.fillRect (juce::Rectangle<float> (fromX, (float) rulerTop(),
                                            juce::jmax (1.0f, toX - fromX), (float) size::rulerHeight)
                        .getIntersection ({ (float) size::gutterTrack, (float) rulerTop(),
                                            (float) getWidth() - (float) size::gutterTrack,
                                            (float) size::rulerHeight }));
    }

    g.setFont (type::font (type::caption));

    for (int bar = painted.getStart(); bar < painted.getEnd(); ++bar)
    {
        const auto x = (float) size::gutterTrack + timeline.xForStep ((double) bar);

        if (x > (float) getWidth())
            break;

        const auto beyond = bar >= bars;

        g.setColour (beyond ? colour::textDisabled : colour::textSecondary);
        g.drawText (juce::String (bar + 1), (int) x + 3, rulerTop(), (int) width - 4, size::rulerHeight,
                    juce::Justification::centredLeft, false);

        g.setColour (beyond ? colour::dividerStrong.withAlpha (emphasis::subdued) : colour::dividerStrong);
        g.drawVerticalLine ((int) x, (float) rulerTop(), (float) bottom);
    }

    // --- the selected span ----------------------------------------------------
    // Drawn over the ruler and down through the tracks, under everything else, so
    // it reads as a region of time rather than as another lane. The playhead gets
    // the same full-height treatment one layer up.
    if (editorState.hasBarSelection())
    {
        const auto selection = editorState.getSelectedBarRange();

        const auto fromX = (float) size::gutterTrack + timeline.xForStep ((double) selection.getStart());
        const auto toX = (float) size::gutterTrack + timeline.xForStep ((double) selection.getEnd());

        const juce::Rectangle<float> content ((float) size::gutterTrack, (float) rulerTop(),
                                              (float) getWidth() - (float) size::gutterTrack,
                                              (float) (bottom - rulerTop()));

        const juce::Rectangle<float> band (fromX, (float) rulerTop(),
                                           juce::jmax (1.0f, toX - fromX),
                                           (float) (bottom - rulerTop()));

        const auto visible = band.getIntersection (content);

        // Only a wash over the tracks; the ruler's solid strip was drawn earlier,
        // under the bar numbers. A wash alone is what this had first, and at an
        // alpha low enough not to bury the clips it was too faint to find -
        // useless for a marker whose whole job is saying what a render will
        // contain. The ruler is where the span can be stated outright without
        // covering anything up.
        g.setColour (colour::accent.withAlpha (emphasis::tint));
        g.fillRect (visible.withTrimmedTop ((float) size::rulerHeight));   // below the ruler strip

        g.setColour (colour::accent);

        for (const auto edge : { fromX, toX })
            if (edge >= (float) size::gutterTrack && edge <= (float) getWidth())
                g.fillRect (edge - stroke::regular * 0.5f, (float) rulerTop(), stroke::regular,
                            (float) (bottom - rulerTop()));
    }

    // --- tracks --------------------------------------------------------------
    const auto trackIndex = paintLanes (g, bottom, anySolo);

    // Past the end of the song.
    const auto endX = (float) size::gutterTrack + timeline.xForStep ((double) bars);

    if (endX < (float) getWidth())
        paint::beyondEnd (g, { (int) endX, lanesTop(), getWidth() - (int) endX,
                               juce::jmax (0, bottom - lanesTop()) }, endX);

    g.setColour (colour::dividerStrong);
    g.drawVerticalLine (size::gutterTrack, (float) rulerTop(), (float) bottom);
    g.drawHorizontalLine (lanesTop() - 1, 0.0f, (float) getWidth());

    // --- playhead ------------------------------------------------------------
    if (engine.getMode() == Transport::Mode::song)
    {
        const auto x = playheadX();

        if (x >= (float) size::gutterTrack)
        {
            playhead.set (engine.isPlaying());

            timelinePaint::playheadLine (g, x, { (float) lanesTop(), (float) bottom },
                                         playhead.brightness());

            // A head on the ruler, so the position is findable at a glance.
            timelinePaint::playheadHead (g, x, (float) lanesTop(), playhead.brightness());
        }
    }

    if (trackIndex == 0)
    {
        paint::emptyState (g, getLocalBounds(), "This project has no playlist tracks");
    }
}

} // namespace dew
