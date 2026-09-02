#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/Gestures.h"

using namespace dew;

namespace
{

juce::MouseWheelDetails wheelOf (float x, float y, bool reversed)
{
    juce::MouseWheelDetails wheel;
    wheel.deltaX = x;
    wheel.deltaY = y;
    wheel.isReversed = reversed;
    wheel.isSmooth = false;
    wheel.isInertial = false;
    return wheel;
}

} // namespace

TEST_CASE ("a wheel notch is read the way the system reports it", "[ui][gesture]")
{
    // JUCE REPORTS natural scrolling rather than applying it, so a handler that
    // ignores isReversed scrolls backwards for anyone running the Mac default.
    // The piano roll was the only one of the three timeline views that read it,
    // which is why the sequencer and the playlist went the wrong way.
    const auto forward = gesture::deltaOf (wheelOf (0.0f, 0.5f, false));
    const auto reversed = gesture::deltaOf (wheelOf (0.0f, 0.5f, true));

    CHECK (forward.y > 0.0);
    CHECK (reversed.y < 0.0);
    CHECK (juce::approximatelyEqual (forward.y, -reversed.y));

    // Both axes, and both flip together - a trackpad reports diagonal movement
    // and reversing only one of them scrolls a view sideways as you swipe up.
    const auto diagonal = gesture::deltaOf (wheelOf (0.25f, 0.5f, true));
    CHECK (diagonal.x < 0.0);
    CHECK (diagonal.y < 0.0);

    // A view that scrolls one way from both axes adds them.
    CHECK (juce::approximatelyEqual (gesture::deltaOf (wheelOf (0.25f, 0.5f, false)).along(), 0.75));
}

TEST_CASE ("a key means the same thing in every timeline view", "[ui][gesture]")
{
    // The playlist bound four keys, the piano roll bound six, and the sequencer
    // and mixer did not override keyPressed at all - so the same key did three
    // different things depending on which tab was in front.
    CHECK (gesture::commandFor (juce::KeyPress ('=')) == gesture::Command::zoomIn);
    CHECK (gesture::commandFor (juce::KeyPress ('-')) == gesture::Command::zoomOut);
    CHECK (gesture::commandFor (juce::KeyPress ('0')) == gesture::Command::zoomToFit);

    CHECK (gesture::commandFor (juce::KeyPress ('1')) == gesture::Command::selectTool);
    CHECK (gesture::commandFor (juce::KeyPress ('2')) == gesture::Command::paintTool);
    CHECK (gesture::commandFor (juce::KeyPress ('3')) == gesture::Command::eraseTool);

    CHECK (gesture::commandFor (juce::KeyPress (juce::KeyPress::escapeKey))
           == gesture::Command::clearSelection);
    CHECK (gesture::commandFor (juce::KeyPress (juce::KeyPress::deleteKey))
           == gesture::Command::deleteSelection);
    CHECK (gesture::commandFor (juce::KeyPress (juce::KeyPress::backspaceKey))
           == gesture::Command::deleteSelection);

    // Select-all takes command OR ctrl. It was command-only in the piano roll,
    // so it did nothing on a machine driven with ctrl even though rubber-band
    // select on the same modifier worked.
    CHECK (gesture::commandFor (juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0))
           == gesture::Command::selectAll);
    CHECK (gesture::commandFor (juce::KeyPress ('a', juce::ModifierKeys::ctrlModifier, 0))
           == gesture::Command::selectAll);

    // A bare letter is not a command: the editors bind q and r themselves, and
    // a map that swallowed them would take them away.
    CHECK (gesture::commandFor (juce::KeyPress ('a')) == gesture::Command::none);
    CHECK (gesture::commandFor (juce::KeyPress ('q')) == gesture::Command::none);
}

TEST_CASE ("shift is finer wherever a drag changes a value", "[ui][gesture]")
{
    // Shift already means five other things in dew - suspend snap, extend a
    // selection, make a copy unique, transpose by an octave - and every one of
    // them changes a selection or a POSITION. None changes a value. That is
    // what keeps the sixth meaning from being one too many.
    CHECK (gesture::isFine (juce::ModifierKeys (juce::ModifierKeys::shiftModifier)));
    CHECK_FALSE (gesture::isFine (juce::ModifierKeys()));

    // And finer means finer, not coarser or the same.
    CHECK (gesture::fineMultiplier > 0.0);
    CHECK (gesture::fineMultiplier < 1.0);
}

TEST_CASE ("zoom answers to command and to control", "[ui][gesture]")
{
    CHECK (gesture::isZoom (juce::ModifierKeys (juce::ModifierKeys::commandModifier)));
    CHECK (gesture::isZoom (juce::ModifierKeys (juce::ModifierKeys::ctrlModifier)));
    CHECK_FALSE (gesture::isZoom (juce::ModifierKeys()));
    CHECK_FALSE (gesture::isZoom (juce::ModifierKeys (juce::ModifierKeys::shiftModifier)));
}

TEST_CASE ("a press becomes a drag only once it has travelled", "[ui][gesture]")
{
    // Component::getDistanceFromDragStart is fed by the real pointer and reads
    // zero in a headless harness, so this is the only form of the rule a test
    // can reach - which is why the effect card's expand-on-click, which used
    // the Component form, had never been tested at all.
    const juce::Point<int> origin { 40, 40 };

    CHECK_FALSE (gesture::passedThreshold (origin, origin));
    CHECK_FALSE (gesture::passedThreshold (origin, origin.translated (gesture::dragThresholdPx - 1, 0)));
    CHECK (gesture::passedThreshold (origin, origin.translated (gesture::dragThresholdPx, 0)));

    // In any direction, not only along an axis.
    CHECK (gesture::passedThreshold (origin, origin.translated (0, -gesture::dragThresholdPx)));
    CHECK (gesture::passedThreshold (origin, origin.translated (10, 10)));
}
