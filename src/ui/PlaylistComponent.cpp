#include "PlaylistComponent.h"

#include "../model/Ids.h"
#include "../model/ProjectEdits.h"
#include "design/Tokens.h"
#include "primitives/DewControls.h"

namespace dew
{

using namespace tokens;

/** One playlist track's header: name, mute and solo.

    Mute and solo are real controls rather than painted text, because the engine
    already honours the properties and there was no way to reach them.
*/
class PlaylistComponent::TrackHeader : public juce::Component
{
public:
    TrackHeader (ProjectDocument& d, juce::ValueTree t)
        : document (d), track (std::move (t))
    {
        nameLabel.setText (track[ids::name].toString(), juce::dontSendNotification);
        nameLabel.setEditable (false, true, false);
        nameLabel.setFont (type::font (type::body));
        nameLabel.setColour (juce::Label::textColourId, colour::textPrimary);
        nameLabel.onTextChange = [this]
        {
            auto& undo = document.getUndoManager();
            undo.beginNewTransaction ("Rename track");
            track.setProperty (ids::name, nameLabel.getText(), &undo);
        };
        addAndMakeVisible (nameLabel);

        muteButton.setToggleState ((bool) track[ids::mute], juce::dontSendNotification);
        muteButton.onClick = [this]
        {
            auto& undo = document.getUndoManager();
            undo.beginNewTransaction ("Mute track");
            track.setProperty (ids::mute, muteButton.getToggleState(), &undo);
        };
        addAndMakeVisible (muteButton);

        soloButton.setToggleState ((bool) track[ids::solo], juce::dontSendNotification);
        soloButton.onClick = [this]
        {
            auto& undo = document.getUndoManager();
            undo.beginNewTransaction ("Solo track");
            track.setProperty (ids::solo, soloButton.getToggleState(), &undo);
        };
        addAndMakeVisible (soloButton);
    }

    void refresh()
    {
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
            g.setColour (colour::wellDeep.withAlpha (0.35f));
            g.fillAll();
        }

        g.setColour (colour::divider);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (space::sm, space::xs);

        soloButton.setBounds (area.removeFromRight (22).reduced (0, space::xxs));
        area.removeFromRight (space::xxs);
        muteButton.setBounds (area.removeFromRight (22).reduced (0, space::xxs));
        area.removeFromRight (space::sm);

        nameLabel.setBounds (area);
    }

private:
    ProjectDocument& document;
    juce::ValueTree track;

    juce::Label nameLabel;
    DewLetterToggle muteButton { "M", colour::warning, "Mute this track" };
    DewLetterToggle soloButton { "S", colour::accent, "Solo this track" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackHeader)
};

// -----------------------------------------------------------------------------

PlaylistComponent::PlaylistComponent (ProjectDocument& d, AudioEngine& e, EditorState& s)
    : document (d), engine (e), editorState (s)
{
    setComponentID ("playlist");
    document.getState().addListener (this);
    editorState.addChangeListener (this);

    horizontalScroll.addListener (this);
    addChildComponent (horizontalScroll);

    addAutomationButton.onClick = [this] { showAutomationMenu(); };
    addAndMakeVisible (addAutomationButton);

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
    updateZoom();
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

int PlaylistComponent::tracksBottom() const
{
    return rulerHeight + getNumTracks() * rowHeight;
}

float PlaylistComponent::contentWidth() const
{
    return (float) juce::jmax (0, getWidth() - headerWidth);
}

int PlaylistComponent::barAtX (int x) const
{
    // Not clamped to the song length: dropping a clip past the end is how an
    // arrangement gets longer. The bars beyond are painted inert but stay
    // writable, exactly as the piano roll treats the end of a pattern.
    return juce::jmax (0, timeline.stepAtX ((float) (x - headerWidth)));
}

int PlaylistComponent::trackAtY (int y) const
{
    return (y - rulerHeight) / rowHeight;
}

juce::Rectangle<float> PlaylistComponent::boundsForClip (const juce::ValueTree& clip, int trackIndex) const
{
    const auto start = (int) clip[ids::startBar];
    const auto length = juce::jmax (1, (int) clip[ids::lengthBars]);

    return { (float) headerWidth + timeline.xForStep ((double) start),
             (float) (rulerHeight + trackIndex * rowHeight),
             (float) (length * timeline.pixelsPerStep),
             (float) rowHeight };
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
    const auto stepsPerBar = juce::jmax (1, (int) document.getState()[ids::stepsPerBeat]) * 4;
    const auto position = engine.getPlayheadSteps() / (double) stepsPerBar;

    return (float) headerWidth + timeline.xForStep (position);
}

// --- layout ------------------------------------------------------------------

void PlaylistComponent::updateZoom()
{
    if (getWidth() <= 0)
        return;

    timeline.pixelsPerStep = juce::jlimit ((double) minBarWidth, (double) maxBarWidth,
                                           (double) contentWidth() / (double) numBars());

    const auto scrollable = timeline.visibleSteps (contentWidth()) < (double) numBars() - 1e-9;
    horizontalScroll.setVisible (scrollable);

    if (! scrollable)
        timeline.scrollOffsetSteps = 0.0;

    timeline.clampScroll (contentWidth(), numBars());

    const juce::ScopedValueSetter<bool> quiet (updatingScrollBar, true);
    horizontalScroll.setRangeLimits (0.0, (double) numBars(), juce::dontSendNotification);
    horizontalScroll.setCurrentRange (timeline.scrollOffsetSteps,
                                      timeline.visibleSteps (contentWidth()), juce::dontSendNotification);
}

void PlaylistComponent::rebuildHeaders()
{
    headers.clear();

    for (const auto& track : playlist())
        if (track.hasType (ids::PLAYLIST_TRACK))
            addAndMakeVisible (headers.add (new TrackHeader (document, track)));

    resized();
}

void PlaylistComponent::resized()
{
    horizontalScroll.setBounds (headerWidth, getHeight() - scrollThickness,
                                (int) contentWidth(), scrollThickness);

    // In the corner above the track headers, where the ruler does not reach.
    addAutomationButton.setBounds (juce::Rectangle<int> (0, 0, headerWidth, rulerHeight)
                                       .reduced (space::xs, space::xxs));

    for (int i = 0; i < headers.size(); ++i)
        headers[i]->setBounds (0, rulerHeight + i * rowHeight, headerWidth, rowHeight);

    updateZoom();
}

void PlaylistComponent::scrollBarMoved (juce::ScrollBar*, double start)
{
    if (updatingScrollBar)
        return;

    timeline.scrollOffsetSteps = start;
    repaint();
}

void PlaylistComponent::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (! horizontalScroll.isVisible())
        return;

    timeline.scrollOffsetSteps -= (double) (wheel.deltaX + wheel.deltaY) * 4.0;
    updateZoom();
    repaint();
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
    const auto stepsPerBar = juce::jmax (1, (int) document.getState()[ids::stepsPerBeat]) * 4;
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
    const auto stepsPerBar = juce::jmax (1, (int) document.getState()[ids::stepsPerBeat]) * 4;
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
        updateZoom();
        repaint();
        return clip;
    }

    // Every lane is occupied there: undo the definition rather than leaving one
    // behind that nothing refers to.
    ProjectEdits::removeAutomation (document.getState(), automation, &undo);
    return {};
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
    if (event.x < headerWidth || event.y < rulerHeight)
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
    if (event.x < headerWidth || event.y < rulerHeight || event.y >= tracksBottom())
        return;

    const auto trackIndex = trackAtY (event.y);
    auto track = trackAt (trackIndex);

    if (! track.isValid())
        return;

    const auto bar = barAtX (event.x);
    auto clip = ProjectEdits::findClipAtBar (track, bar);
    auto& undo = document.getUndoManager();

    if (event.mods.isPopupMenu() || event.mods.isAltDown())
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
            gesture = Gesture::moving;
            dragBarOffset = bar - (int) clip[ids::startBar];
            undo.beginNewTransaction ("Move clip");
        }

        return;
    }

    undo.beginNewTransaction ("Add clip");
    draggedClip = ProjectEdits::addClip (track, editorState.getCurrentPatternId(), bar, 1, &undo);
    draggedClipTrack = track;
    dropTrackIndex = trackIndex;
    gesture = Gesture::resizing;
    ProjectEdits::growSongToFitClips (document.getState(), &undo);
    updateZoom();
    repaint();
}

void PlaylistComponent::mouseDrag (const juce::MouseEvent& event)
{
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
    updateZoom();
    repaint();
}

void PlaylistComponent::mouseUp (const juce::MouseEvent&)
{
    draggedClip = {};
    draggedClipTrack = {};
    draggedPoint = {};
    dropTrackIndex = -1;
    gesture = Gesture::none;
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
    repaint ((int) from - 3, 0, (int) (to - from) + 7, tracksBottom());
}

void PlaylistComponent::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    if (property == ids::barsInSong)
        updateZoom();

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

void PlaylistComponent::paintAutomationClip (juce::Graphics& g, const juce::ValueTree& clip,
                                             int trackIndex, juce::Rectangle<float> bounds,
                                             bool audible)
{
    const auto automation = automationOf (clip);

    // An automation clip reads as a different kind of thing from a pattern
    // clip: no fill, a visible curve, and its own colour.
    const auto clipColour = audible ? colour::warning
                                    : colour::warning.withSaturation (0.1f).withMultipliedBrightness (0.6f);

    g.setColour (colour::wellDeep.withAlpha (0.85f));
    g.fillRoundedRectangle (bounds, radius::sm);
    g.setColour (clipColour.withAlpha (audible ? 0.7f : 0.35f));
    g.drawRoundedRectangle (bounds, radius::sm, stroke::regular);

    if (! automation.isValid())
    {
        g.setColour (colour::danger);
        g.setFont (type::font (type::caption));
        g.drawText ("missing automation", bounds.toNearestInt().reduced (4, 0),
                    juce::Justification::centredLeft, true);
        return;
    }

    // Underneath the curve rather than over it: the curve is the content, and
    // an automation lane is only a row tall.
    g.setColour (clipColour.withAlpha (0.45f));
    g.setFont (type::font (type::caption));
    g.drawText (automation[ids::name].toString(), bounds.toNearestInt().reduced (5, 1),
                juce::Justification::topLeft, true);

    juce::Array<juce::ValueTree> points;

    for (const auto& point : automation)
        if (point.hasType (ids::POINT))
            points.add (point);

    const juce::Graphics::ScopedSaveState clipped (g);
    g.reduceClipRegion (bounds.toNearestInt());

    juce::Path curve;

    for (int i = 0; i < points.size(); ++i)
    {
        const auto position = pointPosition (clip, trackIndex, points[i]);

        if (i == 0)
            curve.startNewSubPath (position);
        else
            curve.lineTo (position);
    }

    if (points.size() > 1)
    {
        g.setColour (clipColour);
        g.strokePath (curve, juce::PathStrokeType (1.6f));
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

void PlaylistComponent::paint (juce::Graphics& g)
{
    const auto bars = numBars();
    const auto width = (float) timeline.pixelsPerStep;
    const auto bottom = tracksBottom();
    const auto visible = timeline.visibleStepRange (contentWidth(), bars);
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
    g.fillRect (0, 0, getWidth(), rulerHeight);
    g.setFont (type::font (type::caption));

    for (int bar = visible.getStart(); bar < visible.getEnd(); ++bar)
    {
        const auto x = (float) headerWidth + timeline.xForStep ((double) bar);

        g.setColour (colour::textSecondary);
        g.drawText (juce::String (bar + 1), (int) x + 3, 0, (int) width - 4, rulerHeight,
                    juce::Justification::centredLeft, false);

        g.setColour (colour::dividerStrong);
        g.drawVerticalLine ((int) x, 0.0f, (float) bottom);
    }

    // --- tracks --------------------------------------------------------------
    int trackIndex = 0;

    for (const auto& track : playlist())
    {
        if (! track.hasType (ids::PLAYLIST_TRACK))
            continue;

        const auto y = rulerHeight + trackIndex * rowHeight;
        const auto audible = ! (bool) track[ids::mute] && (! anySolo || (bool) track[ids::solo]);

        if (trackIndex % 2 == 1)
        {
            g.setColour (colour::wellDeep.withAlpha (0.5f));
            g.fillRect (headerWidth, y, getWidth() - headerWidth, rowHeight);
        }

        // The lane a clip is being dragged onto, so a cross-track drop lands
        // where you meant it to.
        if (gesture == Gesture::moving && trackIndex == dropTrackIndex)
        {
            g.setColour (colour::accent.withAlpha (0.07f));
            g.fillRect (headerWidth, y, getWidth() - headerWidth, rowHeight);
        }

        g.setColour (colour::divider);
        g.drawHorizontalLine (y, (float) headerWidth, (float) getWidth());

        for (const auto& clip : track)
        {
            if (! clip.hasType (ids::CLIP))
                continue;

            const auto bounds = boundsForClip (clip, trackIndex).reduced (2.0f, 3.0f);

            if (! bounds.intersects (juce::Rectangle<float> ((float) headerWidth, 0.0f,
                                                             contentWidth(), (float) bottom)))
                continue;

            if (ProjectEdits::isAutomationClip (clip))
            {
                paintAutomationClip (g, clip, trackIndex, bounds, audible);
                continue;
            }

            const auto pattern = ProjectEdits::findPattern (document.getState(),
                                                            (int) clip[ids::patternId]);

            const auto isCurrent = (int) clip[ids::patternId] == editorState.getCurrentPatternId();
            auto clipColour = isCurrent ? colour::accent : colour::accent.withSaturation (0.35f);

            // A clip on a silenced track is drawn as silenced, so mute and solo
            // are visible in the arrangement and not only in the headers.
            if (! audible)
                clipColour = clipColour.withSaturation (0.1f).withMultipliedBrightness (0.6f);

            g.setColour (clipColour.withAlpha (audible ? 0.75f : 0.4f));
            g.fillRoundedRectangle (bounds, radius::sm);
            g.setColour (clipColour.brighter (0.3f).withAlpha (audible ? 1.0f : 0.5f));
            g.drawRoundedRectangle (bounds, radius::sm, stroke::regular);

            g.setColour (colour::textOnAccent);
            g.setFont (type::font (type::small, true));
            g.drawText (pattern.isValid() ? pattern[ids::name].toString()
                                          : "pattern " + clip[ids::patternId].toString(),
                        bounds.toNearestInt().reduced (5, 0), juce::Justification::centredLeft, true);
        }

        ++trackIndex;
    }

    // Past the end of the song.
    const auto endX = (float) headerWidth + timeline.xForStep ((double) bars);

    if (endX < (float) getWidth())
        paint::inertArea (g, { (int) endX, rulerHeight, getWidth() - (int) endX,
                               juce::jmax (0, bottom - rulerHeight) });

    g.setColour (colour::dividerStrong);
    g.drawVerticalLine (headerWidth, 0.0f, (float) bottom);
    g.drawHorizontalLine (rulerHeight - 1, 0.0f, (float) getWidth());

    // --- playhead ------------------------------------------------------------
    if (engine.getMode() == Transport::Mode::song)
    {
        const auto x = playheadX();

        if (x >= (float) headerWidth)
        {
            g.setColour (engine.isPlaying() ? colour::playhead
                                            : colour::playhead.withAlpha (0.5f));
            g.fillRect (juce::Rectangle<float> (x - 1.0f, (float) rulerHeight,
                                                2.0f, (float) (bottom - rulerHeight)));

            // A head on the ruler, so the position is findable at a glance.
            juce::Path head;
            head.addTriangle (x - 5.0f, (float) rulerHeight - 8.0f,
                              x + 5.0f, (float) rulerHeight - 8.0f,
                              x, (float) rulerHeight);
            g.fillPath (head);
        }
    }

    if (trackIndex == 0)
    {
        paint::emptyState (g, getLocalBounds(), "This project has no playlist tracks");
    }
}

} // namespace dew
