#pragma once

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "model/Ids.h"
#include "app/ProjectDocument.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "ui/EditorState.h"

#include "TestSupport.h"
#include "ui/PianoRollComponent.h"

/** Shared by every piano roll test file, so the two cannot drift into aiming at
    the grid in two slightly different ways.
*/
namespace dew::testing
{

/** Everything a piano roll needs, laid out and visible, so gestures can be
    driven at it directly. Headless: no window, no display.
*/
struct RollHarness
{
    RollHarness (int width = 1200, int height = 700)
    {
        document.setState (ProjectFactory::createDefault(), true);
        roll.setSize (width, height);
        roll.setVisible (true);
        roll.refresh();
        roll.resized();

        // A fixed frame, so every test below aims at the same coordinates
        // whatever the roll would have chosen to open on.
        roll.centreOnPitch (66);
    }

    juce::ValueTree pattern()
    {
        return ProjectEdits::findPattern (document.getState(), 1);
    }

    int countNotes()
    {
        int n = 0;

        for (const auto& child : pattern())
            if (child.hasType (ids::NOTE))
                ++n;

        return n;
    }

    ProjectDocument document;
    AudioEngine engine;
    EditorState editorState;
    PianoRollComponent roll { document, engine, editorState };
};

/** `wasDragged` is the last MouseEvent argument, and it is what
    mouseWasDraggedSinceMouseDown() reports - the real one asks the mouse SOURCE,
    which no synthetic event here ever pressed, so a gesture that distinguishes a
    click from a drag can only be driven by setting it explicitly.
*/
inline juce::MouseEvent eventAt (juce::Component& target, juce::Point<int> local,
                                 juce::ModifierKeys mods = juce::ModifierKeys(), int clickCount = 1,
                                 bool wasDragged = false)
{
    return mouseEventAt (target, local.toFloat(), mods, clickCount, wasDragged);
}

inline void clickAndRelease (juce::Component& c, juce::Point<int> at,
                             juce::ModifierKeys mods = juce::ModifierKeys())
{
    c.mouseDown (eventAt (c, at, mods));
    c.mouseUp (eventAt (c, at, mods));
}

/** Drags from `from` to `to`, reporting `samples` intermediate positions on the
    way - a real drag reports many, and a gesture that only looks at the ends
    behaves differently from one the user actually performs.
*/
inline void dragBetween (juce::Component& c, juce::Point<int> from, juce::Point<int> to,
                         int samples = 8, juce::ModifierKeys mods = juce::ModifierKeys())
{
    c.mouseDown (eventAt (c, from, mods));

    for (int i = 1; i <= samples; ++i)
    {
        const auto t = (float) i / (float) samples;
        c.mouseDrag (eventAt (c,
                              { juce::roundToInt ((float) from.x + t * (float) (to.x - from.x)),
                                juce::roundToInt ((float) from.y + t * (float) (to.y - from.y)) },
                              mods, 1, true));
    }

    // Dragged, so a gesture that asks whether the pointer moved gets the right
    // answer on the release as well as during the drag.
    c.mouseUp (eventAt (c, to, mods, 1, true));
}

/** Where in the component a given step and pitch land.

    Asks the roll where it would paint a note there, rather than recomputing the
    layout - otherwise a layout change leaves these tests clicking confidently
    into the wrong place and still passing.
*/
inline juce::Point<int> pointFor (RollHarness& h, int step, int pitch)
{
    juce::UndoManager scratch;
    auto probe = ProjectEdits::addNote (h.pattern(), 1, step, 1, pitch, 1.0f, &scratch);
    const auto bounds = h.roll.getBoundsForNote (probe);
    ProjectEdits::removeNote (h.pattern(), probe, &scratch);

    // A test aiming outside the visible area is a broken test, not a finding.
    const auto point = juce::Point<int> ((int) (bounds.getX() + bounds.getWidth() * 0.4f),
                                         (int) bounds.getCentreY());
    REQUIRE (h.roll.getNoteArea().contains (point));
    return point;
}

} // namespace dew::testing
