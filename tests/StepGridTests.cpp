// The grid the step editor draws, and where it stops.
//
// Split out of StepGridTests.cpp along its tags. The fixture is
// StepGridHarness.h.

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "ui/design/Tokens.h"

#include "FixtureProject.h"
#include "PaintProbe.h"
#include "StepGridHarness.h"

using namespace dew;
using namespace dew::testing;

TEST_CASE ("the step grid keeps drawing past the end of a short pattern", "[stepgrid][grid]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    GridHarness h;

    // updateZoom caps a cell at 64px, so four steps occupy 256 of 1200 and the
    // rest of the panel is past the pattern end.
    h.setPatternLength (4);

    const auto endX = (int) h.grid.getTimeline().xForStep (4.0);
    const auto rowsHeight = h.grid.getRowsHeight();

    REQUIRE (rowsHeight > 40);
    REQUIRE (endX < h.grid.getWidth() - 200);

    const auto image = h.render();

    const auto columnMean = [&image, rowsHeight] (int x)
    {
        double total = 0.0;

        for (int y = 2; y < rowsHeight - 2; ++y)
            total += image.getPixelAt (x, y).getBrightness();

        return total / juce::jmax (1, rowsHeight - 4);
    };

    juce::Array<double> beyond;

    for (int x = endX + 4; x < h.grid.getWidth() - 2; ++x)
        beyond.add (columnMean (x));

    REQUIRE (beyond.size() > 100);

    auto sorted = beyond;
    sorted.sort();
    const auto background = sorted[sorted.size() / 2];

    int verticalLines = 0;

    for (auto value : beyond)
        if (value > background * 1.15 + 0.002)
            ++verticalLines;

    INFO ("vertical grid lines past the pattern end: " << verticalLines);
    REQUIRE (verticalLines >= 3);

    // And the end itself is marked, so "outside the pattern" is still legible
    // now that it is no longer blanked out.
    const auto meanOver = [&image] (juce::Rectangle<int> area)
    {
        double total = 0.0;
        int counted = 0;

        for (int y = area.getY(); y < area.getBottom(); ++y)
            for (int x = area.getX(); x < area.getRight(); ++x, ++counted)
                total += image.getPixelAt (x, y).getBrightness();

        return total / juce::jmax (1, counted);
    };

    const auto ruleMean = meanOver ({ endX - 1, 2, 3, rowsHeight - 4 });
    const auto nearbyMean = meanOver ({ endX + 20, 2, 3, rowsHeight - 4 });

    INFO ("rule " << ruleMean << " vs nearby " << nearbyMean);
    REQUIRE (ruleMean > nearbyMean * 2.0);
}

TEST_CASE ("the region below the last channel stays inert", "[stepgrid][grid]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    GridHarness h;

    const auto rowsHeight = h.grid.getRowsHeight();

    // Deliberately NOT the same treatment as past the pattern end. There are no
    // rows below the last channel to carry on into, so continuing the grid
    // there would promise steps that do not exist.
    REQUIRE (rowsHeight < h.grid.getHeight() - 40);

    const auto image = h.render();

    double belowTotal = 0.0;
    int counted = 0;

    for (int y = rowsHeight + 8; y < h.grid.getHeight() - 4; ++y)
        for (int x = 4; x < h.grid.getWidth() - 4; x += 3, ++counted)
            belowTotal += image.getPixelAt (x, y).getBrightness();

    const auto belowMean = belowTotal / juce::jmax (1, counted);

    INFO ("mean brightness below the last row: " << belowMean);
    REQUIRE (belowMean > 0.0);
    REQUIRE (belowMean < 0.12);
}
