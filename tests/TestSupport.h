#pragma once

#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace dew::testing
{

/** A synthetic mouse event, in full.

    juce::MouseEvent has a fifteen-argument constructor, and a headless test has
    to fill every one of them. Sixteen places did, in ten files and under four
    different names - eventAt, eventOn, clickAt, and eight anonymous ones - which
    is why no grep found them and why this helper's own comment could claim the
    job was finished while eight copies stood beside it.

    Two traps are worth having in one place. `wasDragged` is the LAST
    constructor argument rather than something the event works out, because
    mouseWasDraggedSinceMouseDown asks the mouse SOURCE and no synthetic event
    ever pressed one. And `mouseDownPos` is separate from `position`: pass the
    same point for both and getDistanceFromDragStart is always zero, which is
    correct for a click and wrong for the drag it silently turns into a click.

    Pointers rather than references for the two components, because a MouseEvent
    with neither is a real case - it is what a handler that reads only the
    modifiers is given.
*/
inline juce::MouseEvent mouseEventOn (juce::Component* eventComponent,
                                      juce::Component* originalComponent,
                                      juce::Point<float> position, juce::Point<float> mouseDownPos,
                                      juce::ModifierKeys mods, int clickCount, bool wasDragged)
{
    const auto now = juce::Time::getCurrentTime();

    return { juce::Desktop::getInstance().getMainMouseSource(),
             position,
             mods,
             1.0f,
             0.0f,
             0.0f,
             0.0f,
             0.0f,
             eventComponent,
             originalComponent,
             now,
             mouseDownPos,
             now,
             clickCount,
             wasDragged };
}

/** The common case: one component, reported where it was pressed.

    Float, and only float, deliberately. An int overload beside it made every
    braced call ambiguous, and `{ 20, 40 }` is how most of these read. With a
    juce::Point<int> in hand, say .toFloat().
*/
inline juce::MouseEvent mouseEventAt (juce::Component& target, juce::Point<float> local,
                                      juce::ModifierKeys mods = juce::ModifierKeys(),
                                      int clickCount = 1, bool wasDragged = false)
{
    return mouseEventOn (&target, &target, local, local, mods, clickCount, wasDragged);
}

/** A drag in progress: reported at `to`, pressed at `from`.

    Separate from mouseEventAt because the difference is the point - a control
    that reads getDistanceFromDragStart sees nothing at all when the two agree.
*/
inline juce::MouseEvent mouseDragEvent (juce::Component& target, juce::Point<float> from,
                                        juce::Point<float> to,
                                        juce::ModifierKeys mods = juce::ModifierKeys())
{
    return mouseEventOn (&target, &target, to, from, mods, 1, true);
}

/** A directory that deletes itself.

    Written out three times, differing only in the prefix string - which is worth
    keeping, because a leaked directory is much easier to trace to its suite when
    it is named after it.
*/
struct TempDir
{
    explicit TempDir (const juce::String& prefix)
        : dir (juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile (prefix + juce::Uuid().toDashedString()))
    {
        dir.createDirectory();
    }

    ~TempDir()
    {
        dir.deleteRecursively();
    }

    TempDir (const TempDir&) = delete;
    TempDir& operator= (const TempDir&) = delete;

    juce::File dir;
};

} // namespace dew::testing
