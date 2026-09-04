// =============================================================================
// PlaylistComponent's menus.
//
// The same class, a second translation unit - the shape PlaylistPaint.cpp
// already uses, and for the same reason: these read a dozen of the playlist's
// members between them, so a free function taking a dozen parameters would not
// be an improvement on a method.
//
// What is here is everything a right-click reaches: the clip menu and what its
// items do, the track menu, the automation-target menu, and the add and remove
// that two of them call. Alongside them the test seams - a menu cannot be
// driven headlessly, so a test reads the built menu and applies a choice by
// number, which is why the ids below are numbered explicitly and appended to
// rather than inserted into.
// =============================================================================

#include "i18n/Strings.h"
#include "ui/PlaylistComponent.h"

#include "model/AutomationTargets.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "ui/MenuSeam.h"
#include "ui/design/Tokens.h"

namespace dew
{

using namespace tokens;

namespace
{
// Numbered EXPLICITLY. These ids are what applyClipMenuChoice takes, so a
// test names an item by its number - and inserting an item in the middle
// would silently re-aim every one of them at something else.
enum class ClipMenuItem
{
    openPattern = 1,
    deleteClip = 2,
    deletePoint = 3,
    addClip = 4,
    duplicatePattern = 5,

    // APPENDED, never inserted. These ids are what applyClipMenuChoice
    // takes, so a test names an item by its number and inserting one in the
    // middle would silently re-aim every one of them.
    shapeLine = 6,
    shapeCurve = 7,
    shapeStep = 8
};
} // namespace

void PlaylistComponent::latchMenuContext (const juce::ValueTree& clip, int trackIndex,
                                          juce::Point<int> position) const
{
    // ONE latch, called by the press and by the test seam alike, so the menu a
    // test reads and the menu the pointer opens cannot be built from different
    // things under the pointer.
    menuPoint = {};
    menuSegment = {};

    if (! clip.isValid())
        return;

    const auto hit = laneHit (clip, trackIndex, position);

    if (hit.kind == automationLane::Hit::Kind::point)
        menuPoint = hit.point;
    else if (hit.kind == automationLane::Hit::Kind::segment)
        menuSegment = hit.point;
}

juce::PopupMenu PlaylistComponent::buildClipMenu (const juce::ValueTree& track, int bar) const
{
    juce::PopupMenu menu;
    const auto clip = ProjectEdits::findClipAtBar (track, bar);

    // The three shapes, flat and ticked - not a submenu. MenuSeam's reader uses
    // MenuItemIterator, which does NOT recurse, so a submenu's children never
    // reach the array a test asserts against. Flat keeps the seam honest and is
    // one click fewer.
    //
    // "Line" is a curve with no bend rather than a third stored shape, so it is
    // ticked when the shape is curve and the bend is zero. Two facts in the file,
    // three choices in the hand.
    const auto addShapeItems = [&menu] (const juce::ValueTree& owner)
    {
        const auto shape = segmentShapeFromString (owner[ids::shape].toString());
        const auto bend = (double) owner[ids::curve];
        const auto straight = shape == SegmentShape::curve && juce::approximatelyEqual (bend, 0.0);

        menu.addItem ((int) ClipMenuItem::shapeLine, tr (StringId::playlist_clip_line), true,
                      straight);
        menu.addItem ((int) ClipMenuItem::shapeCurve, tr (StringId::playlist_clip_curve), true,
                      shape == SegmentShape::curve && ! straight);
        menu.addItem ((int) ClipMenuItem::shapeStep, tr (StringId::playlist_clip_step), true,
                      shape == SegmentShape::step);
    };

    if (menuPoint.isValid())
    {
        menu.addItem ((int) ClipMenuItem::deletePoint, tr (StringId::playlist_clip_deletePoint));
        menu.addSeparator();

        // The shapes of the segment this point OWNS - the one to its right,
        // which is the same segment its bend has always described.
        addShapeItems (menuPoint);
        return menu;
    }

    if (menuSegment.isValid())
    {
        // No "Delete point" here: there is no point under the pointer, and an
        // item that deleted an adjacent one would do something nobody aimed at.
        addShapeItems (menuSegment);
        return menu;
    }

    if (! clip.isValid())
    {
        menu.addItem ((int) ClipMenuItem::addClip, tr (StringId::playlist_clip_addClip));
        return menu;
    }

    // Only a MIDI clip has a pattern to open, so offering it anywhere else
    // would be an item that does nothing on some of the clips in the
    // arrangement.
    if (ProjectEdits::isMidiClip (clip))
    {
        menu.addItem ((int) ClipMenuItem::openPattern, tr (StringId::playlist_clip_openPattern));

        // Gives THIS clip a pattern of its own. A pattern is shared by every
        // clip that names it, so the only way to vary one repeat of a phrase
        // was to make a pattern in the transport bar and re-point the clip by
        // hand.
        menu.addItem ((int) ClipMenuItem::duplicatePattern,
                      tr (StringId::playlist_clip_duplicatePattern));
    }

    if (menu.getNumItems() > 0)
        menu.addSeparator();

    menu.addItem ((int) ClipMenuItem::deleteClip, tr (StringId::playlist_clip_deleteClip));
    return menu;
}

void PlaylistComponent::applyClipChoice (juce::ValueTree track, int bar, int choice)
{
    auto clip = ProjectEdits::findClipAtBar (track, bar);
    auto& undo = document.getUndoManager();

    switch ((ClipMenuItem) choice)
    {
        case ClipMenuItem::openPattern: openPatternOf (clip); break;

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
                    if (auto fresh = ProjectEdits::duplicatePattern (document.getState(), pattern,
                                                                     &undo);
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

        case ClipMenuItem::shapeLine:
        case ClipMenuItem::shapeCurve:
        case ClipMenuItem::shapeStep:
            if (auto owner = menuPoint.isValid() ? menuPoint : menuSegment; owner.isValid())
            {
                undo.beginNewTransaction ("Change segment shape");

                if ((ClipMenuItem) choice == ClipMenuItem::shapeLine)
                    ProjectEdits::setPointStraight (owner, &undo);
                else
                    ProjectEdits::setPointShape (owner,
                                                 (ClipMenuItem) choice == ClipMenuItem::shapeStep
                                                     ? SegmentShape::step
                                                     : SegmentShape::curve,
                                                 &undo);
            }
            break;

        case ClipMenuItem::addClip:
            undo.beginNewTransaction ("Add clip");
            ProjectEdits::addClip (track, editorState.getCurrentPatternId(), bar, 1, &undo);
            ProjectEdits::growSongToFitClips (document.getState(), &undo);
            updateScrollBar();
            break;

        default: break;
    }

    menuPoint = {};
    menuSegment = {};
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

    ConfirmPanel::Request request;
    request.title = "Remove track";
    request.message = "Remove \"" + track[ids::name].toString()
                      + "\"? Every clip on it goes with it.";

    // A playlist track carries no id - they are positional, unlike channels and
    // patterns - so the INDEX is what survives the dialog, and it is resolved
    // again on the way back rather than a ValueTree being held across it.
    auto index = -1;

    for (int i = 0; i < getNumTracks(); ++i)
        if (trackAt (i) == track)
            index = i;

    if (index < 0)
        return;

    confirmDestructive (request, [this, index] { removeTrackNow (index); });
}

void PlaylistComponent::removeTrackNow (int index)
{
    auto track = trackAt (index);

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

juce::StringArray PlaylistComponent::clipMenuItemsAt (juce::Point<int> position) const
{
    const auto trackIndex = trackAtY (position.y);
    const auto track = trackAt (trackIndex);
    const auto bar = barAtX (position.x);
    const auto clip = track.isValid() ? ProjectEdits::findClipAtBar (track, bar)
                                      : juce::ValueTree();

    latchMenuContext (clip, trackIndex, position);

    const auto menu = buildClipMenu (track, bar);

    return menuItems (menu);
}

juce::StringArray PlaylistComponent::clipMenuItems (int trackIndex, int bar) const
{
    // Reimplemented ON the position pair rather than kept as a second builder: a
    // bar has no y, and which segment of a curve you are on IS a y. Aiming at
    // the middle of the row is what asking about a bar has always meant.
    juce::UndoManager scratch;
    auto probe = ProjectEdits::addClip (trackAt (trackIndex), 1, bar, 1, &scratch);
    const auto bounds = boundsForClip (probe, trackIndex);
    ProjectEdits::removeClip (trackAt (trackIndex), probe, &scratch);

    return clipMenuItemsAt (bounds.getCentre().toInt());
}

bool PlaylistComponent::applyClipMenuChoiceAt (juce::Point<int> position, int choice)
{
    const auto trackIndex = trackAtY (position.y);
    auto track = trackAt (trackIndex);

    if (! track.isValid())
        return false;

    const auto bar = barAtX (position.x);

    latchMenuContext (ProjectEdits::findClipAtBar (track, bar), trackIndex, position);
    applyClipChoice (track, bar, choice);
    return true;
}

bool PlaylistComponent::applyClipMenuChoice (int trackIndex, int bar, int choice)
{
    auto track = trackAt (trackIndex);

    if (! track.isValid())
        return false;

    juce::UndoManager scratch;
    auto probe = ProjectEdits::addClip (track, 1, bar, 1, &scratch);
    const auto bounds = boundsForClip (probe, trackIndex);
    ProjectEdits::removeClip (track, probe, &scratch);

    return applyClipMenuChoiceAt (bounds.getCentre().toInt(), choice);
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
        const auto group = target.displayName.upToFirstOccurrenceOf (
            tr (StringId::automation_separator), false, false);

        if (group != currentGroup)
        {
            if (currentGroup.isNotEmpty())
                menu.addSubMenu (currentGroup, submenu);

            submenu.clear();
            currentGroup = group;
        }

        submenu.addItem (itemId++, target.displayName.fromFirstOccurrenceOf (
                                       tr (StringId::automation_separator), false, false));
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

} // namespace dew
