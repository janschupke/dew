#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "ui/TimelineView.h"

using namespace dew;
using Catch::Matchers::WithinAbs;

TEST_CASE ("steps and pixels round-trip through the scroll offset", "[timeline]")
{
    TimelineView view;
    view.pixelsPerStep = 20.0;
    view.scrollOffsetSteps = 0.0;

    REQUIRE_THAT (view.xForStep (0.0), WithinAbs (0.0, 1e-9));
    REQUIRE_THAT (view.xForStep (4.0), WithinAbs (80.0, 1e-9));
    REQUIRE (view.stepAtX (0.0f) == 0);
    REQUIRE (view.stepAtX (19.0f) == 0);
    REQUIRE (view.stepAtX (20.0f) == 1);

    view.scrollOffsetSteps = 8.0;
    REQUIRE_THAT (view.xForStep (8.0), WithinAbs (0.0, 1e-9));
    REQUIRE (view.stepAtX (0.0f) == 8);
    REQUIRE (view.stepAtX (45.0f) == 10);

    // Left of the content area is off the start of the material, not step -2.
    REQUIRE (view.stepAtX (-200.0f) == 0);
}

TEST_CASE ("zooming keeps the step under the pointer where it was", "[timeline]")
{
    TimelineView view;
    view.pixelsPerStep = 20.0;
    view.scrollOffsetSteps = 6.0;

    const auto anchorX = 300.0f;
    const auto before = view.stepForX (anchorX);

    view.zoomAround (2.0, anchorX);
    REQUIRE_THAT (view.stepForX (anchorX), WithinAbs (before, 1e-9));
    REQUIRE_THAT (view.pixelsPerStep, WithinAbs (40.0, 1e-9));

    view.zoomAround (0.25, anchorX);
    REQUIRE_THAT (view.stepForX (anchorX), WithinAbs (before, 1e-9));
    REQUIRE_THAT (view.pixelsPerStep, WithinAbs (10.0, 1e-9));
}

TEST_CASE ("zoom stops at its limits without moving the view", "[timeline]")
{
    TimelineView view;
    view.pixelsPerStep = TimelineView::maxPixelsPerStep;
    view.scrollOffsetSteps = 3.0;

    view.zoomAround (4.0, 100.0f);
    REQUIRE_THAT (view.pixelsPerStep, WithinAbs (TimelineView::maxPixelsPerStep, 1e-9));
    REQUIRE_THAT (view.scrollOffsetSteps, WithinAbs (3.0, 1e-9));

    view.pixelsPerStep = TimelineView::minPixelsPerStep;
    view.zoomAround (0.1, 100.0f);
    REQUIRE_THAT (view.pixelsPerStep, WithinAbs (TimelineView::minPixelsPerStep, 1e-9));
    REQUIRE_THAT (view.scrollOffsetSteps, WithinAbs (3.0, 1e-9));
}

TEST_CASE ("the view cannot scroll past the material, in either direction", "[timeline]")
{
    TimelineView view;
    view.pixelsPerStep = 10.0; // 40 steps visible in 400px
    view.scrollOffsetSteps = 500.0;

    view.clampScroll (400.0f, 64);
    REQUIRE_THAT (view.scrollOffsetSteps, WithinAbs (24.0, 1e-9)); // 64 - 40

    view.scrollOffsetSteps = -12.0;
    view.clampScroll (400.0f, 64);
    REQUIRE_THAT (view.scrollOffsetSteps, WithinAbs (0.0, 1e-9));

    // Material narrower than the view pins to the start rather than floating.
    view.scrollOffsetSteps = 5.0;
    view.clampScroll (400.0f, 16);
    REQUIRE_THAT (view.scrollOffsetSteps, WithinAbs (0.0, 1e-9));
}

TEST_CASE ("following the playhead scrolls the least it can", "[timeline]")
{
    TimelineView view;
    view.pixelsPerStep = 10.0; // 40 steps visible in 400px
    view.scrollOffsetSteps = 0.0;

    // Already comfortably in view: do not move at all.
    view.ensureVisible (20.0, 400.0f);
    REQUIRE_THAT (view.scrollOffsetSteps, WithinAbs (0.0, 1e-9));

    // Off the right: bring it just inside, not to the centre.
    view.ensureVisible (60.0, 400.0f);
    REQUIRE_THAT (view.scrollOffsetSteps, WithinAbs (21.0, 1e-9));

    // Off the left: same, from the other side.
    view.ensureVisible (5.0, 400.0f);
    REQUIRE_THAT (view.scrollOffsetSteps, WithinAbs (4.0, 1e-9));
}

TEST_CASE ("painting only walks the steps that are on screen", "[timeline]")
{
    TimelineView view;
    view.pixelsPerStep = 10.0;
    view.scrollOffsetSteps = 100.0;

    const auto range = view.visibleStepRange (400.0f, 256);

    REQUIRE (range.getStart() == 100);
    REQUIRE (range.getEnd() <= 142); // 40 visible plus a step of slack
    REQUIRE (range.getEnd() >= 140);

    // A range longer than the material still stops at the material.
    view.scrollOffsetSteps = 0.0;
    view.pixelsPerStep = 1.0;
    const auto whole = view.visibleStepRange (4000.0f, 64);
    REQUIRE (whole.getStart() == 0);
    REQUIRE (whole.getEnd() == 64);
}

TEST_CASE ("fitting a pattern shows all of it from the start", "[timeline]")
{
    TimelineView view;
    view.scrollOffsetSteps = 30.0;

    view.fit (32, 640.0f);
    REQUIRE_THAT (view.pixelsPerStep, WithinAbs (20.0, 1e-9));
    REQUIRE_THAT (view.scrollOffsetSteps, WithinAbs (0.0, 1e-9));
    REQUIRE_THAT (view.visibleSteps (640.0f), WithinAbs (32.0, 1e-9));

    // A pattern too long to fit at the minimum zoom clamps rather than vanishing.
    view.fit (100000, 640.0f);
    REQUIRE_THAT (view.pixelsPerStep, WithinAbs (TimelineView::minPixelsPerStep, 1e-9));
}
