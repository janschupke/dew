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
#include "ui/StepGridComponent.h"

/** A step grid laid out and visible, so a cell can be clicked at directly.

    Shared by the grid's painting tests and its gesture tests, for the reason
    every harness here exists: both aim at a cell by asking the component where
    it is, and two ways of doing that is one of them going stale.
*/
namespace dew::testing
{

using namespace dew;

struct GridHarness
{
    GridHarness (int width = 1200, int height = 400)
    {
        document.setState (ProjectFactory::createDefault(), true);
        grid.setSize (width, height);
        grid.setVisible (true);
        grid.resized();
    }

    juce::ValueTree pattern()
    {
        return ProjectEdits::findPattern (document.getState(), 1);
    }

    /** The channel drawn on row 0. Channels are direct children of PROJECT and
        the grid draws them in tree order, so the first one is the top row.
    */
    juce::ValueTree firstChannel()
    {
        return document.getState().getChildWithName (ids::CHANNEL);
    }

    void setPatternLength (int steps)
    {
        pattern().setProperty (ids::lengthSteps, steps, nullptr);
        grid.resized();
    }

    juce::Image render()
    {
        juce::Image image (juce::Image::ARGB, grid.getWidth(), grid.getHeight(), true);
        juce::Graphics g (image);
        grid.paintEntireComponent (g, true);
        return image;
    }

    ProjectDocument document;
    AudioEngine engine;
    EditorState editorState;
    StepGridComponent grid { document, engine, editorState };
};

/** The centre of a cell, ASKED of the grid rather than recomputed.

    The comment on this harness has claimed since it was written that both its
    users "aim at a cell by asking the component where it is". Until
    getBoundsForCell existed there was nothing to ask, and the two of them each
    rebuilt the rectangle from xForStep and rowHeight instead - which is the
    duplication the claim was about.
*/
inline juce::Point<int> pointFor (GridHarness& h, int step, int row)
{
    const auto cell = h.grid.getBoundsForCell (row, step);

    REQUIRE (cell.getWidth() > 0.0f);
    REQUIRE (cell.getBottom() <= (float) h.grid.getRowsHeight());

    return cell.getCentre().roundToInt();
}

inline juce::MouseEvent eventAt (juce::Component& target, juce::Point<int> local,
                                 juce::ModifierKeys mods = juce::ModifierKeys())
{
    return mouseEventAt (target, local, mods, 1, false);
}

} // namespace dew::testing
