#pragma once

#include <span>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/design/Tokens.h"

namespace dew
{

/** Rows of captioned knobs that reflow, in groups that stay together.

    dew wrote this five times - InstrumentPanel, OscillatorFaces,
    SoundFontSection, SampleSection and EffectCard each had a lambda that
    divided a rectangle by a COMPILE-TIME count - and having written them
    separately they had already drifted. The instrument panel divided its width
    by four for the envelope and by two for the levels directly below, so the
    same control was drawn at two sizes one above the other, at every width;
    the effect card asked for three columns whether it had two parameters or
    four, so a card was either a third empty or laid out 3 + 1 with a hole in
    it. Nothing anywhere re-wrapped: only the cells got narrower.

    Two things this owns that a lambda cannot:

      - **One cell width across the whole grid**, taken from the row that can
        afford the least. Volume is the size attack is whether they share a row
        with it or not, which is the whole of what "the knobs are different
        sizes" was.
      - **Groups.** A knob belongs to one - the envelope, the levels, an
        effect's own parameters as against the `mix` every effect has - and a
        group is never split while it fits a row. Two groups sharing a row are
        separated by a rule; two on different rows are separated by the row
        break and need no rule. That is the whole grammar, and it is why this
        answers in runs rather than in a cell count.

    Pure arithmetic: counts and a rectangle in, rectangles out, so it is tested
    with no components and no window at all. RowView is the same decision for
    the vertical axis of a timeline. The rules are x positions the caller
    paints, which is StripLayout's split - the layout says where, the painter
    says how.
*/
class KnobGrid
{
public:
    /** Between two cells of one group. */
    static constexpr int cellGap = tokens::space::xxs;

    /** Between two groups sharing a row, with the rule down the middle of it. */
    static constexpr int groupGap = tokens::space::lg;

    /** How the grid falls out: each row, as the runs of cells that share it.

        A run is a whole group, or as much of one as fitted. Asked before
        anything is placed, so a panel's height budget and its resized() read
        one answer rather than two written to mirror each other -
        InstrumentPanel::getRequiredHeight and EffectCard::getRequiredHeight
        both kept that mirror by hand, and both are what this replaces.
    */
    struct Plan
    {
        std::vector<std::vector<int>> rows;
        int cellWidth = tokens::size::knobColumn;

        int numRows() const noexcept
        {
            return (int) rows.size();
        }
    };

    /** The plan for a band of this WIDTH: the rows fall out of it.

        Fewest rows first, then the widest column that still fits in them. The
        other way round spends height to widen a knob nobody asked to be wider,
        which in the sidebar means scrolling for it.
    */
    static Plan planForWidth (int width, std::span<const int> groupSizes)
    {
        Plan plan;

        if (groupSizes.empty())
            return plan;

        // The most cells a row can hold is the most it can hold at the
        // narrowest column. Group breaks are not charged here and are in
        // cellWidthFor, which is the one that has to be right: this only picks
        // how many rows, and a row that turns out to owe a break pays for it in
        // a slightly narrower cell rather than in another row.
        const auto capacity = juce::jmax (1, (width + cellGap)
                                                 / (tokens::size::knobColumnMin + cellGap));

        plan.rows = pack (capacity, groupSizes);
        plan.cellWidth = cellWidthFor (width, plan.rows);

        return plan;
    }

    /** The plan for a band this many ROWS deep.

        The mixer's card, which is as wide as it has to be and no wider - the
        band's height is the constraint there and the row is scrolled sideways.
        Never more rows than asked for: a card taller than the band it sits in
        is clipped, and the point of dragging the band taller is that the cards
        get NARROWER.

        `width` narrows the cells to fit one, for the caller whose row structure
        is fixed but whose width is not: a soundfont's two rows of three are two
        rows at every size, and all they want from this is the cell width every
        other knob in the panel is getting. Zero means "as wide as they like",
        which is the card's case.
    */
    static Plan planForRows (int rows, std::span<const int> groupSizes, int width = 0)
    {
        Plan plan;

        auto cells = 0;

        for (const auto size : groupSizes)
            cells += juce::jmax (0, size);

        if (cells == 0)
            return plan;

        const auto wanted = juce::jmax (1, rows);

        // Keeping a group whole can cost a row - three pairs over two rows is
        // the small case - so the capacity is opened up until the packing fits
        // the budget. It always terminates: at `cells` everything is one row.
        for (auto capacity = (cells + wanted - 1) / wanted; capacity <= cells; ++capacity)
        {
            plan.rows = pack (capacity, groupSizes);

            if (plan.numRows() <= wanted)
                break;
        }

        if (width > 0)
            plan.cellWidth = cellWidthFor (width, plan.rows);

        return plan;
    }

    /** How tall a plan is, rows and the gaps between them. */
    static int heightFor (const Plan& plan) noexcept
    {
        const auto rows = plan.numRows();

        return rows <= 0 ? 0 : rows * tokens::size::knobRow + (rows - 1) * tokens::space::sm;
    }

    /** How wide a plan is at its own cell width: its widest row. */
    static int widthFor (const Plan& plan) noexcept
    {
        auto widest = 0;

        for (const auto& row : plan.rows)
            widest = juce::jmax (widest, rowWidth (row, plan.cellWidth));

        return widest;
    }

    struct Placement
    {
        /** One per control, in group order - the order the groups were given
            in, and within a group the order its controls were given in. */
        std::vector<juce::Rectangle<int>> cells;

        /** The break between two groups sharing a row. The caller draws a rule
            down the middle of each: the layout says where, the painter says
            how, exactly as StripLayout::divider does for a strip. */
        std::vector<juce::Rectangle<int>> rules;
    };

    /** Lays the plan out in `band` and answers where everything went.

        Cells rather than bounds, because what a cell HOLDS is the caller's:
        a knob fills its cell and a number field is a fixed-height control
        centred in it, since stretching one only makes a tall empty box.
    */
    static Placement place (juce::Rectangle<int> band, const Plan& plan)
    {
        Placement placement;

        auto area = band;

        for (int r = 0; r < plan.numRows(); ++r)
        {
            const auto& row = plan.rows[(size_t) r];

            if (r > 0)
                area.removeFromTop (tokens::space::sm);

            auto strip = area.removeFromTop (tokens::size::knobRow);

            // What is left when the cells and the breaks have had theirs,
            // shared out AROUND the groups: the end, each break, the end. One
            // group in a row is therefore centred and two are pushed apart,
            // which is what makes a row read as its groups rather than as a
            // line of knobs that happens to have a rule in it.
            const auto slack = juce::jmax (0, strip.getWidth() - rowWidth (row, plan.cellWidth));
            const auto share = slack / ((int) row.size() + 1);

            strip.removeFromLeft (share);

            for (size_t g = 0; g < row.size(); ++g)
            {
                if (g > 0)
                    placement.rules.push_back (strip.removeFromLeft (groupGap + share));

                for (int c = 0; c < row[g]; ++c)
                {
                    if (c > 0)
                        strip.removeFromLeft (cellGap);

                    placement.cells.push_back (strip.removeFromLeft (plan.cellWidth));
                }
            }
        }

        return placement;
    }

    /** How wide one row is at this cell width. */
    static int rowWidth (const std::vector<int>& row, int cellWidth) noexcept
    {
        const auto cells = cellsIn (row);

        if (cells <= 0)
            return 0;

        return cells * cellWidth + (cells - (int) row.size()) * cellGap
               + ((int) row.size() - 1) * groupGap;
    }

private:
    static int cellsIn (const std::vector<int>& row) noexcept
    {
        auto cells = 0;

        for (const auto run : row)
            cells += run;

        return cells;
    }

    /** Fills rows `capacity` cells wide, keeping a group whole while it fits.

        A group too big for a whole row is the one thing that splits, and it
        splits rather than being hidden: WCAG 1.4.10 asks that content reflow
        rather than be lost, and a knob nothing can reach is lost. The run it
        leaves behind still starts a new group in whatever row it lands in, so
        the rule that separates it from what follows is drawn either way.
    */
    static std::vector<std::vector<int>> pack (int capacity, std::span<const int> groupSizes)
    {
        std::vector<std::vector<int>> rows;

        const auto room = juce::jmax (1, capacity);
        auto used = room; // so the first group opens a row

        for (const auto size : groupSizes)
        {
            auto left = juce::jmax (0, size);

            // A group that would fit a row of its own opens one rather than
            // being torn in half by the end of this one.
            if (left > 0 && left <= room && used + left > room)
                used = room;

            while (left > 0)
            {
                if (used >= room)
                {
                    rows.emplace_back();
                    used = 0;
                }

                const auto take = juce::jmin (left, room - used);

                rows.back().push_back (take);
                used += take;
                left -= take;
            }
        }

        return rows;
    }

    /** The widest cell every row of this packing can afford, capped at the
        column the design system names. Uncapped, six knobs in a wide panel
        would be six enormous knobs rather than six knobs and some air. */
    static int cellWidthFor (int width, const std::vector<std::vector<int>>& rows) noexcept
    {
        auto best = tokens::size::knobColumn;

        for (const auto& row : rows)
        {
            const auto cells = cellsIn (row);

            if (cells <= 0)
                continue;

            const auto fixed = (cells - (int) row.size()) * cellGap
                               + ((int) row.size() - 1) * groupGap;

            best = juce::jmin (best, (width - fixed) / cells);
        }

        return juce::jmax (1, best);
    }
};

} // namespace dew
