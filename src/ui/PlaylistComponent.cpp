#include "PlaylistComponent.h"

#include "engine/SamplePool.h"

#include "model/Ids.h"
#include "model/Meter.h"
#include "model/ProjectEdits.h"
#include "TimelineRuler.h"
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

        // setEditable does not stop a Label eating clicks - it only touches
        // keyboard focus - so without this the label swallows every press across
        // the header's whole left side and the row's own mouseDown never runs.
        // The channel rack learned this the hard way; see the README.
        nameLabel.setInterceptsMouseClicks (false, false);
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

    /** What the header's context menu offers, and what each item does.

        Named methods rather than a lambda inside showMenuAsync, because
        showMenuAsync cannot be driven headlessly and every other gesture here is
        tested that way. The menu is only how a person reaches these.
    */
    enum class MenuItem { rename = 1, addTrack, removeTrack };

    juce::PopupMenu buildMenu() const
    {
        juce::PopupMenu menu;
        menu.addItem ((int) MenuItem::rename, "Rename");
        menu.addItem ((int) MenuItem::addTrack, "Add track");
        menu.addSeparator();
        menu.addItem ((int) MenuItem::removeTrack, "Remove track");
        return menu;
    }

    void applyMenuChoice (int choice)
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

    void mouseDown (const juce::MouseEvent& event) override
    {
        if (! event.mods.isPopupMenu())
            return;

        auto menu = buildMenu();

        menu.setLookAndFeel (&getLookAndFeel());
        menu.showMenuAsync (juce::PopupMenu::Options()
                                .withTargetScreenArea ({ event.getScreenX(), event.getScreenY(), 1, 1 }),
                            [safe = juce::Component::SafePointer<TrackHeader> (this)] (int choice)
                            {
                                if (safe != nullptr && choice > 0)
                                    safe->applyMenuChoice (choice);
                            });
    }

    void mouseDoubleClick (const juce::MouseEvent& event) override
    {
        if (nameLabel.getBounds().contains (event.getPosition()))
            nameLabel.showEditor();
    }

    juce::ValueTree getTrack() const { return track; }

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

    addTrackButton.onClick = [this] { addTrack(); };
    addTrackButton.setComponentID ("addTrackButton");
    addAndMakeVisible (addTrackButton);

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
    addAndMakeVisible (toolbar);

    setWantsKeyboardFocus (true);

    // The ruler works in BARS here, so everything the gesture is handed is in
    // bars and only the seek converts - one place, rather than a conversion in
    // each of the three branches this used to have.
    rulerGesture.unitForX = [this] (int x)
    {
        return juce::jlimit (0.0, (double) numBars(),
                             timeline.stepForX ((float) (x - headerWidth)));
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

int PlaylistComponent::tracksBottom() const
{
    return lanesTop() + getNumTracks() * rowHeight;
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
    return (y - lanesTop()) / rowHeight;
}

juce::Rectangle<float> PlaylistComponent::boundsForClip (const juce::ValueTree& clip, int trackIndex) const
{
    const auto start = (int) clip[ids::startBar];
    const auto length = juce::jmax (1, (int) clip[ids::lengthBars]);

    return { (float) headerWidth + timeline.xForStep ((double) start),
             (float) (lanesTop() + trackIndex * rowHeight),
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
    const auto stepsPerBar = Meter::of (document.getState()).stepsPerBar();
    const auto position = engine.getPlayheadSteps() / (double) stepsPerBar;

    return (float) headerWidth + timeline.xForStep (position);
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

    const juce::ScopedValueSetter<bool> quiet (updatingScrollBar, true);
    horizontalScroll.setRangeLimits (0.0, (double) numBars(), juce::dontSendNotification);
    horizontalScroll.setCurrentRange (timeline.scrollOffsetSteps, visible,
                                      juce::dontSendNotification);
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
            addAndMakeVisible (header);
        }

    resized();
}

void PlaylistComponent::resized()
{
    toolbar.setBounds (0, 0, getWidth(), toolbarHeight);

    horizontalScroll.setBounds (headerWidth, getHeight() - scrollThickness,
                                (int) contentWidth(), scrollThickness);

    // In the corner above the track headers, where the ruler does not reach.
    addAutomationButton.setBounds (juce::Rectangle<int> (0, rulerTop(), headerWidth, rulerHeight)
                                       .reduced (space::xs, space::xxs));

    for (int i = 0; i < headers.size(); ++i)
        headers[i]->setBounds (0, lanesTop() + i * rowHeight, headerWidth, rowHeight);

    // Directly below the last track, and never over the horizontal scrollbar -
    // an add button you cannot reach because a scrollbar is on top of it is the
    // same as no add button.
    const auto buttonTop = lanesTop() + headers.size() * rowHeight;
    const auto room = getHeight() - scrollThickness - buttonTop;

    addTrackButton.setVisible (room >= rowHeight);
    addTrackButton.setBounds (juce::Rectangle<int> (0, buttonTop, headerWidth, rowHeight)
                                  .reduced (space::sm, space::xs));

    // Until someone has zoomed or scrolled, a layout frames the whole song.
    // Latching after the FIRST layout instead would hand the zoom to whatever
    // size the component happened to be built at, which is not the size it ends
    // up; re-fitting always is what made the playlist unzoomable.
    if (! viewIsUsers && getWidth() > 0)
        timeline.fit (numBars(), contentWidth());

    updateScrollBar();
}

void PlaylistComponent::scrollBarMoved (juce::ScrollBar*, double start)
{
    if (updatingScrollBar)
        return;

    viewIsUsers = true;
    timeline.scrollOffsetSteps = start;
    repaint();
}

void PlaylistComponent::mouseWheelMove (const juce::MouseEvent& event,
                                        const juce::MouseWheelDetails& wheel)
{
    // Mod-wheel zooms around the pointer, exactly as it does over the piano
    // roll. The playlist answered only to scrolling, and only when there was
    // something to scroll.
    if (event.mods.isCommandDown() || event.mods.isCtrlDown())
    {
        zoomBy (std::pow (2.0, (double) wheel.deltaY * 3.0),
                (float) (event.x - headerWidth));
        return;
    }

    viewIsUsers = true;
    timeline.scrollOffsetSteps -= (double) (wheel.deltaX + wheel.deltaY) * 4.0;
    updateScrollBar();
    repaint();
}

void PlaylistComponent::mouseMagnify (const juce::MouseEvent& event, float scaleFactor)
{
    zoomBy ((double) scaleFactor, (float) (event.x - headerWidth));
}

bool PlaylistComponent::keyPressed (const juce::KeyPress& key)
{
    if (key.getTextCharacter() == '+' || key.getTextCharacter() == '=')
    {
        zoomBy (1.5, contentWidth() * 0.5f);
        return true;
    }

    if (key.getTextCharacter() == '-' || key.getTextCharacter() == '_')
    {
        zoomBy (1.0 / 1.5, contentWidth() * 0.5f);
        return true;
    }

    if (key.getTextCharacter() == '1')
    {
        toolbar.setTool (PlaylistTool::select);
        return true;
    }

    if (key.getTextCharacter() == '2')
    {
        toolbar.setTool (PlaylistTool::paint);
        return true;
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
                        clip.setProperty (ids::patternId, (int) fresh[ids::id], &undo);
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

    juce::StringArray items;

    // Named local: MenuItemIterator keeps a REFERENCE to the menu, so iterating
    // a temporary walks a destroyed object and silently yields nothing.
    const auto menu = headers[trackIndex]->buildMenu();

    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        items.add (it.getItem().isSeparator ? "-" : it.getItem().text);

    return items;
}

juce::StringArray PlaylistComponent::clipMenuItems (int trackIndex, int bar) const
{
    juce::StringArray items;
    const auto menu = buildClipMenu (trackAt (trackIndex), bar);

    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        items.add (it.getItem().isSeparator ? "-" : it.getItem().text);

    return items;
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
    if (event.x >= headerWidth && event.y >= rulerTop() && event.y < lanesTop())
    {
        rulerGesture.mouseDoubleClick (event);
        return;
    }

    if (event.x < headerWidth || event.y < lanesTop())
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
    // The ruler used to be excluded outright by this guard, so the playlist's
    // was as inert as the piano roll's. Everything it does now lives in
    // ruler::Gesture, which is why this is one line rather than three branches
    // the piano roll also had a copy of.
    if (event.x >= headerWidth && event.y >= rulerTop() && event.y < lanesTop())
    {
        rulerGesture.mouseDown (event);
        return;
    }

    if (event.x < headerWidth || event.y < lanesTop() || event.y >= tracksBottom())
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
    if (position.x < headerWidth || position.y < lanesTop() || position.y >= tracksBottom())
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
    repaint ((int) from - 3, rulerTop(), (int) (to - from) + 7, tracksBottom() - rulerTop());
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
                          ? juce::Colour::fromString ("ff" + channel[ids::colour].toString().getLastCharacters (6))
                          : colour::textDisabled;

    if (! audible)
        clipColour = clipColour.withSaturation (0.1f).withMultipliedBrightness (0.6f);

    g.setColour (clipColour.withAlpha (audible ? 0.35f : 0.2f));
    g.fillRoundedRectangle (bounds, radius::sm);
    g.setColour (clipColour.brighter (0.3f).withAlpha (audible ? 1.0f : 0.5f));
    g.drawRoundedRectangle (bounds, radius::sm, stroke::regular);

    auto* pool = engine.getSamplePool();
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

            const auto centre = bounds.getCentreY();
            const auto halfHeight = bounds.getHeight() * 0.5f - 2.0f;

            g.setColour (clipColour.brighter (0.4f).withAlpha (audible ? 0.9f : 0.4f));

            for (int x = (int) bounds.getX(); x < (int) bounds.getRight(); ++x)
            {
                const auto a = ((float) x - bounds.getX()) / bounds.getWidth();
                const auto b = ((float) (x + 1) - bounds.getX()) / bounds.getWidth();

                const auto bin = entry.peaks.range (a, b);
                const auto top = centre - bin.maximum * halfHeight;
                const auto bottom = centre - bin.minimum * halfHeight;

                g.fillRect ((float) x, juce::jmin (top, bottom), 1.0f,
                            juce::jmax (1.0f, std::abs (bottom - top)));
            }
        }
    }

    g.setColour (colour::textPrimary.withAlpha (audible ? 0.9f : 0.5f));
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
    g.fillRect (0, rulerTop(), getWidth(), rulerHeight);

    // The selection's strip goes down before the bar numbers, so the numbers
    // inside it stay legible instead of being washed out by it.
    if (editorState.hasBarSelection())
    {
        const auto selection = editorState.getSelectedBarRange();

        const auto fromX = (float) headerWidth + timeline.xForStep ((double) selection.getStart());
        const auto toX = (float) headerWidth + timeline.xForStep ((double) selection.getEnd());

        g.setColour (colour::accent.withAlpha (0.55f));
        g.fillRect (juce::Rectangle<float> (fromX, (float) rulerTop(),
                                            juce::jmax (1.0f, toX - fromX), (float) rulerHeight)
                        .getIntersection ({ (float) headerWidth, (float) rulerTop(),
                                            (float) getWidth() - (float) headerWidth,
                                            (float) rulerHeight }));
    }

    g.setFont (type::font (type::caption));

    for (int bar = painted.getStart(); bar < painted.getEnd(); ++bar)
    {
        const auto x = (float) headerWidth + timeline.xForStep ((double) bar);

        if (x > (float) getWidth())
            break;

        const auto beyond = bar >= bars;

        g.setColour (beyond ? colour::textDisabled : colour::textSecondary);
        g.drawText (juce::String (bar + 1), (int) x + 3, rulerTop(), (int) width - 4, rulerHeight,
                    juce::Justification::centredLeft, false);

        g.setColour (beyond ? colour::dividerStrong.withAlpha (0.35f) : colour::dividerStrong);
        g.drawVerticalLine ((int) x, (float) rulerTop(), (float) bottom);
    }

    // --- the selected span ----------------------------------------------------
    // Drawn over the ruler and down through the tracks, under everything else, so
    // it reads as a region of time rather than as another lane. The playhead gets
    // the same full-height treatment one layer up.
    if (editorState.hasBarSelection())
    {
        const auto selection = editorState.getSelectedBarRange();

        const auto fromX = (float) headerWidth + timeline.xForStep ((double) selection.getStart());
        const auto toX = (float) headerWidth + timeline.xForStep ((double) selection.getEnd());

        const juce::Rectangle<float> content ((float) headerWidth, (float) rulerTop(),
                                              (float) getWidth() - (float) headerWidth,
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
        g.setColour (colour::accent.withAlpha (0.10f));
        g.fillRect (visible.withTrimmedTop ((float) rulerHeight));   // below the ruler strip

        g.setColour (colour::accent);

        for (const auto edge : { fromX, toX })
            if (edge >= (float) headerWidth && edge <= (float) getWidth())
                g.fillRect (edge - stroke::regular * 0.5f, (float) rulerTop(), stroke::regular,
                            (float) (bottom - rulerTop()));
    }

    // --- tracks --------------------------------------------------------------
    int trackIndex = 0;

    for (const auto& track : playlist())
    {
        if (! track.hasType (ids::PLAYLIST_TRACK))
            continue;

        const auto y = lanesTop() + trackIndex * rowHeight;
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

            if (! bounds.intersects (juce::Rectangle<float> ((float) headerWidth, (float) lanesTop(),
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
        paint::beyondEnd (g, { (int) endX, lanesTop(), getWidth() - (int) endX,
                               juce::jmax (0, bottom - lanesTop()) }, endX);

    g.setColour (colour::dividerStrong);
    g.drawVerticalLine (headerWidth, (float) rulerTop(), (float) bottom);
    g.drawHorizontalLine (lanesTop() - 1, 0.0f, (float) getWidth());

    // --- playhead ------------------------------------------------------------
    if (engine.getMode() == Transport::Mode::song)
    {
        const auto x = playheadX();

        if (x >= (float) headerWidth)
        {
            g.setColour (engine.isPlaying() ? colour::playhead
                                            : colour::playhead.withAlpha (0.5f));
            g.fillRect (juce::Rectangle<float> (x - 1.0f, (float) lanesTop(),
                                                2.0f, (float) (bottom - lanesTop())));

            // A head on the ruler, so the position is findable at a glance.
            juce::Path head;
            head.addTriangle (x - 5.0f, (float) lanesTop() - 8.0f,
                              x + 5.0f, (float) lanesTop() - 8.0f,
                              x, (float) lanesTop());
            g.fillPath (head);
        }
    }

    if (trackIndex == 0)
    {
        paint::emptyState (g, getLocalBounds(), "This project has no playlist tracks");
    }
}

} // namespace dew
