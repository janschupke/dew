// What the right mouse button is allowed to do, which is: open a menu, erase in
// the two grids, or nothing at all.
//
// Every control in dew refused a popup press in mouseDown and that was half a
// rule - juce::Button re-arms itself on a drag and completes the click on the
// release - so a right-press that moved one pixel fired every button in the
// application. A walk rather than a source gate, because "overrides mouseDown
// but not mouseUp" is not a thing a scanner can see.

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/Ids.h"
#include "ui/MainComponent.h"
#include "ui/primitives/DewControls.h"

#include "ControlWalkHarness.h"

using namespace dew;
using namespace dew::testing;

namespace
{

/** A press, a small move and a release, all on the right button.

    The move is what makes this a test rather than a repeat of the mouseDown
    one: two pixels is under gesture::dragThresholdPx, so nothing here is a drag
    by dew's own reckoning, and it was still enough to re-arm a juce::Button.

    wasDragged is the LAST MouseEvent argument and has to be set by hand -
    mouseWasDraggedSinceMouseDown asks the mouse SOURCE, which no synthetic
    event ever pressed.
*/
void rightClick (juce::Component& target)
{
    const auto mods = juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier);
    const auto centre = target.getLocalBounds().getCentre().toFloat();
    const auto moved = centre.translated (2.0f, 2.0f);

    const auto event = [&] (juce::Point<float> position, bool wasDragged)
    {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), position, mods,
                                 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &target, &target,
                                 juce::Time::getCurrentTime(), position,
                                 juce::Time::getCurrentTime(), 1, wasDragged);
    };

    target.mouseDown (event (centre, false));
    target.mouseDrag (event (moved, true));
    target.mouseUp (event (moved, true));
}

} // namespace

TEST_CASE ("a popup press is refused for the whole press, not just the press",
           "[ui][gesture][rightclick]")
{
    // The unit, because the WINDOW cannot be asked this one. juce::Button reads
    // isMouseSourceOver to decide whether a drag re-arms it, and that asks
    // Component::isMouseOver - the real pointer, which a headless harness has
    // not got. So a synthetic right-drag over a real button never reaches the
    // buttonDown state the defect needs, and a walk of the window would report
    // every button well behaved whether or not it was.
    //
    // What CAN be stated here is the rule itself, and the reason it is a latch.
    const auto press = [] (juce::ModifierKeys mods)
    {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), {}, mods, 1.0f,
                                 0.0f, 0.0f, 0.0f, 0.0f, nullptr, nullptr,
                                 juce::Time::getCurrentTime(), {}, juce::Time::getCurrentTime(), 1,
                                 false);
    };

    const auto right = juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier);
    const auto left = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier);

    PopupPress guard;

    CHECK (guard.down (press (right), nullptr));
    CHECK (guard.dragging());

    // The whole reason it is remembered rather than re-read: ctrl-click is how
    // macOS spells a right-click, and the modifiers on the RELEASE are the
    // modifiers as they are then. A control that asked again would refuse the
    // press and then complete the click.
    CHECK (guard.releasing());

    // And the latch is spent, so the next press starts from nothing.
    CHECK_FALSE (guard.dragging());
    CHECK_FALSE (guard.releasing());

    CHECK_FALSE (guard.down (press (left), nullptr));
    CHECK_FALSE (guard.dragging());
    CHECK_FALSE (guard.releasing());

    // A hook is opened once, on the press, and never on the release.
    auto opened = 0;
    const std::function<void()> hook = [&opened] { ++opened; };

    CHECK (guard.down (press (right), hook));
    CHECK (opened == 1);
    CHECK (guard.releasing());
    CHECK (opened == 1);

    // With NO hook the press is still refused, which is the half this used to
    // get wrong: a person aiming at a menu that is not there asked for nothing,
    // not for the button.
    CHECK (guard.down (press (right), nullptr));
}

TEST_CASE ("no control in the window changes its value on a right-drag",
           "[ui][gesture][rightclick]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    MainComponent component (false);
    component.setSize (1400, 900);
    component.resized();

    juce::StringArray moved;
    auto sliders = 0;

    // Every juce::Slider under the window rather than forEachControl's list:
    // the one this was written for is the mixer's fader, which is a slider
    // outright, and a knob's is a CHILD of the control the walk would find.
    //
    // juce::Slider only treats a right press as a menu when setPopupMenuEnabled
    // is on, and nothing in dew turns it on - so the press fell through to the
    // drag branch and moved the value. A knob, a fader and two octave steppers
    // all did it, and each wrote an undo step for a gesture nobody made.
    for (int tab = 0; tab < Settings::numTabs; ++tab)
    {
        component.showTab (tab);
        component.resized();

        walk (component,
              [&] (juce::Component& c)
              {
                  auto* slider = dynamic_cast<juce::Slider*> (&c);

                  if (slider == nullptr || ! slider->isEnabled()
                      || slider->getMaximum() <= slider->getMinimum())
                      return;

                  ++sliders;

                  const auto before = slider->getValue();
                  rightClick (*slider);

                  if (! juce::exactlyEqual (slider->getValue(), before))
                      moved.add (describe (c));
              });
    }

    // Control case: a walk that found no sliders would report every one of them
    // well behaved.
    INFO ("sliders walked: " << sliders);
    REQUIRE (sliders > 10);

    INFO ("controls whose value a right-drag moved:\n" << moved.joinIntoString ("\n"));
    CHECK (moved.isEmpty());
}

TEST_CASE ("a press forwarded from a child is not a press on its parent",
           "[ui][gesture][rightclick]")
{
    // forwardChildMouseEventsTo registers a row as a MouseListener on every
    // descendant, and JUCE delivers the press to the control AND to its
    // listeners - carrying the SAME MouseEvent, whose eventComponent and whose
    // coordinates are the child's. It exists for hover: a strip should light up
    // while the pointer is over its fader.
    //
    // Three rows read those forwarded events as presses on themselves and
    // opened a context menu from their own mouseDown, so a right-click on a
    // mixer strip's fader, its pan knob or its M and S opened the CONTROL's
    // parameter menu and the STRIP's menu, one on top of the other. The channel
    // rack did the same through HeaderRow.
    //
    // The composite cannot be driven here - both menus go through
    // showMenuAsync, which needs a ComponentPeer, and is why every menu in dew
    // is built by a named method and read through MenuSeam instead. What is
    // stated here is the predicate the two rows now ask, and the thing about it
    // that is easy to get wrong: it is the event's own component, not a hit
    // test against the parent's bounds, which a forwarded press would pass.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    juce::Component parent;
    juce::Component child;

    parent.setSize (200, 400);
    parent.addAndMakeVisible (child);
    child.setBounds (10, 10, 60, 200);

    const auto pressOn = [] (juce::Component& target)
    {
        const auto position = juce::Point<float> (5.0f, 5.0f);

        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), position,
                                 juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier), 1.0f,
                                 0.0f, 0.0f, 0.0f, 0.0f, &target, &target,
                                 juce::Time::getCurrentTime(), position,
                                 juce::Time::getCurrentTime(), 1, false);
    };

    CHECK (isOwnPress (pressOn (parent), parent));
    CHECK_FALSE (isOwnPress (pressOn (child), parent));

    // And the coordinates are the trap that goes with it: a forwarded press
    // carries the CHILD's position, so measuring it against the parent's own
    // regions answers about a point that was never pressed.
    CHECK (pressOn (child).getPosition() == juce::Point<int> (5, 5));
}
