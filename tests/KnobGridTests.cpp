#include <catch2/catch_test_macros.hpp>

#include "ui/KnobGrid.h"

using namespace dew;
using namespace dew::tokens;

/*  The knob grid, with no components and no window in it at all.

    That is the whole reason it is arithmetic rather than a base class: the
    defect it exists to end - the instrument panel drawing four envelope knobs
    at a quarter of its width and the two level knobs below them at a half - is
    a statement about numbers, and a test that had to build a panel to make it
    would be testing the panel.
*/

namespace
{

/** Every cell the grid placed, widest first, so a test can say "one size". */
std::vector<int> cellWidthsOf (const KnobGrid::Placement& placed)
{
    std::vector<int> widths;

    for (const auto& cell : placed.cells)
        widths.push_back (cell.getWidth());

    return widths;
}

bool allEqual (const std::vector<int>& values)
{
    for (const auto value : values)
        if (value != values.front())
            return false;

    return ! values.empty();
}

/** How many distinct row tops the placement used. */
int rowsUsed (const KnobGrid::Placement& placed)
{
    std::vector<int> tops;

    for (const auto& cell : placed.cells)
        if (std::find (tops.begin(), tops.end(), cell.getY()) == tops.end())
            tops.push_back (cell.getY());

    return (int) tops.size();
}

} // namespace

TEST_CASE ("one cell width across every row of a grid", "[ui][knobgrid]")
{
    // The defect, stated directly. Four envelope knobs and two level knobs, in
    // a panel too narrow to hold all six on one line: the two rows hold
    // different numbers of knobs and the knobs are the same size anyway.
    const std::vector<int> groups { 4, 2 };

    const juce::Rectangle<int> band { 0, 0, 284, 400 };
    const auto plan = KnobGrid::planForWidth (band.getWidth(), groups);
    const auto placed = KnobGrid::place (band, plan);

    REQUIRE (placed.cells.size() == 6);
    REQUIRE (rowsUsed (placed) == 2);

    const auto widths = cellWidthsOf (placed);

    INFO ("cell widths: " << widths[0] << " " << widths[4]);
    CHECK (allEqual (widths));

    // And the control case: the old layout is what this would have answered if
    // the width were divided per row. A volume knob twice an attack knob is
    // exactly what must not come back.
    CHECK (widths[4] < 2 * widths[0]);
}

TEST_CASE ("a wide band puts both groups on one row and rules between them", "[ui][knobgrid]")
{
    const std::vector<int> groups { 4, 2 };

    const juce::Rectangle<int> band { 0, 0, 624, 400 };
    const auto placed = KnobGrid::place (band, KnobGrid::planForWidth (band.getWidth(), groups));

    REQUIRE (rowsUsed (placed) == 1);

    // Two groups sharing a row is exactly one rule, and it sits between the
    // last knob of the first group and the first of the second.
    REQUIRE (placed.rules.size() == 1);
    CHECK (placed.rules[0].getX() >= placed.cells[3].getRight());
    CHECK (placed.rules[0].getRight() <= placed.cells[4].getX());
}

TEST_CASE ("groups on rows of their own are separated by the row break alone", "[ui][knobgrid]")
{
    // The other half of the grammar. A rule as well as a row break would be
    // saying the same thing twice, and the panel would carry a rule at every
    // width rather than only where one is needed.
    const std::vector<int> groups { 4, 2 };

    const juce::Rectangle<int> band { 0, 0, 284, 400 };
    const auto placed = KnobGrid::place (band, KnobGrid::planForWidth (band.getWidth(), groups));

    REQUIRE (rowsUsed (placed) == 2);
    CHECK (placed.rules.empty());
}

TEST_CASE ("a cell is never wider than the column the design system names", "[ui][knobgrid]")
{
    // The leftover goes into the gaps AROUND the groups, not into the cells.
    // Six knobs across a very wide panel are six knobs and some air, not six
    // enormous knobs - which is the same defect as the one above, at the other
    // end of the range.
    const std::vector<int> groups { 2 };

    const juce::Rectangle<int> band { 0, 0, 1200, 200 };
    const auto plan = KnobGrid::planForWidth (band.getWidth(), groups);
    const auto placed = KnobGrid::place (band, plan);

    REQUIRE (placed.cells.size() == 2);
    CHECK (plan.cellWidth == size::knobColumn);

    for (const auto& cell : placed.cells)
        CHECK (cell.getWidth() == size::knobColumn);

    // Centred, because one group in a row is spread around: the air either side
    // of it is equal to within the pixel the division could not split.
    const auto before = placed.cells.front().getX() - band.getX();
    const auto after = band.getRight() - placed.cells.back().getRight();

    INFO ("air: " << before << " before, " << after << " after");
    CHECK (std::abs (before - after) <= 1);
}

TEST_CASE ("a group too wide for a row splits, and nothing is lost", "[ui][knobgrid]")
{
    // WCAG 1.4.10 asks that content reflow rather than be lost. A knob nothing
    // can reach is lost, so the group that cannot fit is the one thing that
    // breaks - and every knob still gets a cell.
    const std::vector<int> groups { 6 };

    const juce::Rectangle<int> band { 0, 0, 200, 400 };
    const auto placed = KnobGrid::place (band, KnobGrid::planForWidth (band.getWidth(), groups));

    CHECK (placed.cells.size() == 6);
    CHECK (rowsUsed (placed) > 1);

    for (const auto& cell : placed.cells)
        CHECK (cell.getWidth() > 0);
}

TEST_CASE ("a row budget is never exceeded, however the groups fall", "[ui][knobgrid]")
{
    // The mixer's card: the band's depth is fixed and the card is as wide as
    // that leaves it. A card taller than its band is clipped, so this is the
    // one direction the grid may not give way in - and keeping a group whole
    // can cost a row, which is what makes it worth asserting.
    const std::vector<std::vector<int>> cases { { 5, 1 }, { 2, 2, 2 }, { 1, 1 }, { 3, 1 } };

    for (const auto& groups : cases)
    {
        for (auto rows = size::effectBandRowsMin; rows <= size::effectBandRowsMax; ++rows)
        {
            const auto plan = KnobGrid::planForRows (rows, groups);

            auto cells = 0;

            for (const auto& row : plan.rows)
                for (const auto run : row)
                    cells += run;

            auto wanted = 0;

            for (const auto size : groups)
                wanted += size;

            INFO ("groups " << groups.size() << " into " << rows << " rows");
            CHECK (plan.numRows() <= rows);
            CHECK (cells == wanted);
        }
    }
}

TEST_CASE ("a deeper band makes a card narrower", "[ui][knobgrid]")
{
    // What dragging the mixer's effect band taller is FOR. The same parameters
    // on two rows are half as wide, so twice as many effects fit across.
    const std::vector<int> groups { 5, 1 };

    const auto oneRow = KnobGrid::widthFor (KnobGrid::planForRows (1, groups));
    const auto twoRows = KnobGrid::widthFor (KnobGrid::planForRows (2, groups));

    INFO ("one row " << oneRow << "px, two rows " << twoRows << "px");
    CHECK (twoRows < oneRow);
    CHECK (KnobGrid::heightFor (KnobGrid::planForRows (2, groups))
           > KnobGrid::heightFor (KnobGrid::planForRows (1, groups)));
}

TEST_CASE ("what a plan says it needs is what placing it uses", "[ui][knobgrid]")
{
    // The drift this exists to stop: a panel budgets its height from planFor
    // and lays out with place, and the two were hand-mirrored arithmetic in
    // three components before there was one answer to mirror.
    const std::vector<int> groups { 4, 2 };

    for (const auto width : { 204, 284, 380, 624 })
    {
        const juce::Rectangle<int> band { 0, 0, width, 600 };
        const auto plan = KnobGrid::planForWidth (width, groups);
        const auto placed = KnobGrid::place (band, plan);

        INFO ("at " << width << "px");
        REQUIRE (rowsUsed (placed) == plan.numRows());

        // Nothing hangs off the right edge, which is the other half of "what it
        // said it needed": a cell width that fitted on paper and not in the band
        // would be a grid that clipped in silence.
        for (const auto& cell : placed.cells)
            CHECK (cell.getRight() <= band.getRight());

        const auto lowest = placed.cells.back().getBottom();

        CHECK (lowest - band.getY() <= KnobGrid::heightFor (plan));
    }
}

TEST_CASE ("an empty grid asks for nothing", "[ui][knobgrid]")
{
    // A card whose type declares no parameters at all. planForWidth used to be
    // a divide by zero and an infinite loop in the layout it replaced.
    const std::vector<int> none;

    const auto plan = KnobGrid::planForWidth (300, none);

    CHECK (plan.numRows() == 0);
    CHECK (KnobGrid::heightFor (plan) == 0);
    CHECK (KnobGrid::widthFor (plan) == 0);
    CHECK (KnobGrid::place ({ 0, 0, 300, 300 }, plan).cells.empty());
}
