// How a timeline thins out as it zooms away from you.
//
// Its own file because the subject is shared: one stride helper, and three
// painters - the piano roll's grid, TimelineRuler and the playlist's own copy
// of that ruler - which have to agree about which bars survive. A test living
// in any one of the three would only ever cover that one.

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/GridDensity.h"
#include "ui/TimelineView.h"
#include "ui/ZoomButtons.h"
#include "ui/design/Tokens.h"

#include "PaintProbe.h"
#include "PlaylistHarness.h"
#include "RollHarness.h"

using namespace dew;
using namespace dew::testing;

namespace
{

/** Columns that are brighter than the view's own background, counted down the
    whole height so a grid LINE registers and a note or a label does not.
*/
int gridLinesAcross (const juce::Image& image, juce::Rectangle<int> area)
{
    const auto columnMean = [&image, area] (int x)
    {
        double total = 0.0;

        for (int y = area.getY() + 4; y < area.getBottom() - 4; ++y)
            total += image.getPixelAt (x, y).getBrightness();

        return total / juce::jmax (1, area.getHeight() - 8);
    };

    juce::Array<double> columns;

    for (int x = area.getX() + 2; x < area.getRight() - 2; ++x)
        columns.add (columnMean (x));

    auto sorted = columns;
    sorted.sort();
    const auto background = sorted[sorted.size() / 2];

    auto lines = 0;

    for (auto value : columns)
        if (value > background * 1.15 + 0.002)
            ++lines;

    return lines;
}

} // namespace

TEST_CASE ("a stride only ever removes marks", "[grid][density]")
{
    /*  Powers of two is the property the painters lean on: every stride's
        marks are a subset of the one below it, so zooming out takes lines away
        and never slides them sideways. A 1, 2, 3, 5 progression would put the
        third bar line in a different place at two adjacent zooms.
    */
    using gridDensity::strideFor;

    // Already far enough apart, so nothing is spent.
    CHECK (strideFor (40.0, 28.0) == 1);
    CHECK (strideFor (28.0, 28.0) == 1);

    // And the ladder from there.
    CHECK (strideFor (27.0, 28.0) == 2);
    CHECK (strideFor (14.0, 28.0) == 2);
    CHECK (strideFor (13.0, 28.0) == 4);
    CHECK (strideFor (1.0, 28.0) == 32);

    // Whatever the input, a stride is a positive power of two, and it really
    // does clear the spacing it was asked for.
    for (double px = 0.05; px < 60.0; px *= 1.07)
    {
        const auto stride = strideFor (px, gridDensity::labelSpacingPx);

        INFO ("at " << px << " pixels per unit the stride is " << stride);
        REQUIRE (stride >= 1);
        CHECK ((stride & (stride - 1)) == 0);
        CHECK ((double) stride * px >= gridDensity::labelSpacingPx);

        // And it is the SMALLEST such stride - one rung down would not do.
        if (stride > 1)
            CHECK ((double) (stride / 2) * px < gridDensity::labelSpacingPx);
    }

    // Nothing pathological out of a degenerate view.
    CHECK (strideFor (0.0, 28.0) >= 1);
    CHECK (strideFor (-1.0, 28.0) >= 1);
}

TEST_CASE ("zooming a canvas out thins its grid instead of filling it", "[grid][density]")
{
    /*  The bar and beat tiers used to be drawn at every bar and every beat
        whatever the zoom, so the bottom of the range was a solid block of
        divider colour rather than a grid. Counted from pixels rather than from
        the painter's own arithmetic, which is the only measurement that would
        have caught it.
    */
    const juce::ScopedJuceInitialiser_GUI juceInit;

    RollHarness h { 1200, 700 };
    h.pattern().setProperty (ids::lengthSteps, 4096, nullptr);
    h.roll.refresh();

    const auto area = h.roll.getNoteArea();

    int atDefault = 0, atFloor = 0;

    {
        h.roll.applyView (24.0, 0.0, (double) tokens::size::pianoRowDefault);
        atDefault = gridLinesAcross (render (h.roll), area);
    }

    {
        h.roll.applyView (TimelineView::minPixelsPerStep, 0.0,
                          (double) tokens::size::pianoRowDefault);
        atFloor = gridLinesAcross (render (h.roll), area);
    }

    // What the strides actually promise: no tier is ever drawn closer together
    // than the spacing it is named for. Stated as lines per width rather than
    // as a comparison between the two zooms, because the finest tier drawn is
    // a different one at each - steps at 24 px/step, bars at the floor - and
    // "fewer than the other view" would be a coincidence rather than the rule.
    const auto roomiest = (double) area.getWidth() / gridDensity::barSpacingPx;

    INFO ("grid lines at 24 px/step: " << atDefault << ", at the floor ("
                                       << TimelineView::minPixelsPerStep << " px/step): " << atFloor
                                       << ", and " << area.getWidth() << "px holds at most "
                                       << roomiest << " at the bar spacing");

    // There is still a grid down there.
    CHECK (atFloor > 4);

    // And it is a grid rather than a fill. Before the strides this was 700-odd
    // lines in a 1146px window - one every step, at every zoom.
    CHECK ((double) atFloor <= roomiest);
}

TEST_CASE ("the arrangement is still numbered at the bottom of its zoom", "[grid][density]")
{
    /*  The bar numbers were an all-or-nothing cutoff at 28px per bar: below it
        every number in the window went at once, so the view that most needs
        something to navigate by was the one view with nothing on it.

        Asserted on ink in the ruler strip rather than on the numbers
        themselves - what matters is that the reader is not left with a bare
        band, and reading the digits back would be asserting the font.
    */
    const juce::ScopedJuceInitialiser_GUI juceInit;

    PlaylistHarness h { 1200, 500 };

    // Driven through the toolbar, which is the seam the user reaches the zoom
    // by - and far enough out that it lands on the clamp whatever it started
    // at, so this asserts the bottom of the range rather than a place near it.
    for (int i = 0; i < 40; ++i)
        h.playlist.getToolbar().onZoom (1.0 / ZoomButtons::zoomFactor);

    REQUIRE (juce::exactlyEqual (h.playlist.getTimeline().pixelsPerStep,
                                 TimelineView::minPixelsPerStep));

    const auto image = render (h.playlist);
    const auto ruler = h.playlist.getRulerArea();

    auto ink = 0;

    for (int y = ruler.getY() + 2; y < ruler.getBottom() - 2; ++y)
        for (int x = ruler.getX() + 2; x < ruler.getRight() - 2; ++x)
            if (image.getPixelAt (x, y).getBrightness() > 0.45f)
                ++ink;

    INFO ("bright pixels in the ruler at " << TimelineView::minPixelsPerStep
                                           << " px/step: " << ink);
    CHECK (ink > 40);
}
