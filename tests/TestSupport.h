#pragma once

#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace dew::testing
{

/** A synthetic mouse event on a component, in its own coordinates.

    Three harnesses - the playlist's, the piano roll's and the step grid's -
    each spelled this out, in three argument orders that suit their own call
    sites. The ORDERS are theirs and stay theirs; the body is not, and it holds
    two traps worth having in one place: `wasDragged` is the LAST constructor
    argument rather than something the event works out, because
    mouseWasDraggedSinceMouseDown asks the mouse SOURCE and no synthetic event
    ever pressed one - and `position` is passed as mouseDownPos too, which is
    why getDistanceFromDragStart is always zero here.

    Each of those three now calls this. Their signatures are unchanged, so no
    test moved.
*/
inline juce::MouseEvent mouseEventAt (juce::Component& target, juce::Point<int> local,
                                      juce::ModifierKeys mods, int clickCount, bool wasDragged)
{
    const auto position = local.toFloat();

    return { juce::Desktop::getInstance().getMainMouseSource(),
             position,
             mods,
             1.0f,
             0.0f,
             0.0f,
             0.0f,
             0.0f,
             &target,
             &target,
             juce::Time::getCurrentTime(),
             position,
             juce::Time::getCurrentTime(),
             clickCount,
             wasDragged };
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
