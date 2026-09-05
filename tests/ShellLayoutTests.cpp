// The window's chrome, measured in the pixels it actually paints.
//
// The transport strip, the editor tab bar and the seam between the editor and
// the instrument panel are the three regions a person sees before they see
// anything dew does, and each had drifted in a way no assertion could catch:
// the transport's controls sat six pixels from the top of a forty-six pixel bar
// and fourteen from the bottom, the rule between two tabs was missing exactly
// where the selected tab was, and the seam was two rules with a column of
// window background between them.
//
// Painted offscreen and counted, the way every other UI test here works - see
// PaintProbe.h.

#include <cmath>

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/MainComponent.h"
#include "ui/design/Tokens.h"

#include "PaintProbe.h"

using namespace dew;
using namespace dew::testing;

namespace
{

/** Where the content of a horizontal strip begins and ends.

    A whole span of columns rather than one, because a control is a rounded
    rectangle and its own left edge is background for the first row or two.
*/
juce::Range<int> inkRows (const juce::Image& image, juce::Range<int> columns, juce::Range<int> rows,
                          juce::Colour ground)
{
    auto first = -1, last = -1;

    for (auto y = rows.getStart(); y < rows.getEnd(); ++y)
    {
        auto inked = false;

        for (auto x = columns.getStart(); x < columns.getEnd() && ! inked; ++x)
            inked = image.getPixelAt (x, y).withAlpha (1.0f) != ground.withAlpha (1.0f);

        if (! inked)
            continue;

        if (first < 0)
            first = y;

        last = y;
    }

    return { first, last + 1 };
}

} // namespace

TEST_CASE ("the transport's controls are centred in the strip", "[ui][shell]")
{
    // StripLayout::place used to say withHeight, which keeps the TOP edge of
    // the slot it was given - so a 26px control in a 46px bar inset by six sat
    // with six pixels above it and fourteen below. The two editor toolbars
    // never showed it: a 34px strip inset by four leaves exactly a control's
    // height, and there is nothing to centre.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    MainComponent component (false);
    component.setSize (1400, 900);

    const auto image = render (component);

    // The bar, less the rule it draws along its own bottom edge.
    const juce::Range<int> bar { 0, tokens::size::stripTransport - tokens::stroke::hairlinePx };

    // Across the transport buttons at the left end, which are the first things
    // the strip places.
    const auto ink = inkRows (image, { tokens::space::md, tokens::size::gutterLabel }, bar,
                              tokens::colour::surface);

    REQUIRE (ink.getStart() >= 0);

    const auto above = ink.getStart() - bar.getStart();
    const auto below = bar.getEnd() - ink.getEnd();

    INFO ("bar " << bar.getStart() << ".." << bar.getEnd() << ", ink " << ink.getStart() << ".."
                 << ink.getEnd() << ", above " << above << ", below " << below);

    CHECK (above > 0);

    // Within a pixel: a control's height and the strip's need not share parity.
    CHECK (std::abs (above - below) <= 1);
}

TEST_CASE ("every boundary between two tabs carries a rule", "[ui][shell]")
{
    // The rule used to be drawn on an INACTIVE tab's trailing edge, so the
    // boundary beside the selected tab had none at all: the active tab drew no
    // rule, and the tab before it is the one that would have. It was also inset
    // six pixels top and bottom, which put a short stroke beside a block of
    // colour running the bar's full height.
    // Through the window rather than a bare EditorTabs: a TabbedComponent
    // painted in isolation puts a line of its own along its top edge, which is
    // not something the application ever shows.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    MainComponent component (false);
    component.setSize (1400, 900);

    auto* tabs = dynamic_cast<juce::TabbedComponent*> (component.findChildWithID ("editorTabs"));
    REQUIRE (tabs != nullptr);

    auto& bar = tabs->getTabbedButtonBar();
    REQUIRE (bar.getNumTabs() >= 3);

    // Selected in the middle, so BOTH of its boundaries are tested - the one
    // the old rule was missing from and the one it was not.
    component.showTab (1);

    const auto image = render (component);

    for (int i = 1; i < bar.getNumTabs(); ++i)
    {
        auto* button = bar.getTabButton (i);
        REQUIRE (button != nullptr);

        // The button's own rows, less the ones the active tab's underline
        // takes: those belong to the tab, not to the boundary. Measured against
        // the button rather than against the bar, because "the same depth the
        // tab's background does" is what the rule has to match.
        // In the image's coordinates, which are the window's.
        const auto area = component.getLocalArea (button, button->getLocalBounds());
        const auto rows = juce::Range<int> (
            area.getY(), area.getBottom() - juce::roundToInt (tokens::stroke::bold));

        // The boundary column, against one a little way inside the same tab. A
        // rule differs from its own tab's ground for the whole depth; a rule
        // inset top and bottom differs for part of it, and a missing one for
        // none.
        auto found = 0;

        for (auto y = rows.getStart(); y < rows.getEnd(); ++y)
            if (image.getPixelAt (area.getX(), y)
                != image.getPixelAt (area.getX() + tokens::space::sm, y))
                ++found;

        INFO ("boundary before tab " << i << " at x=" << area.getX() << ": " << found << " of "
                                     << rows.getLength() << " rows");

        CHECK (found == rows.getLength());
    }
}

TEST_CASE ("one rule between the editor and the panel, and nothing between them", "[ui][shell]")
{
    // The seam was a sixteen-pixel strip of window background with a rule down
    // its middle, and the panel drew a SECOND rule on its own left edge - two
    // lines with a gap between them, which is what "two redundant gaps" was.
    // The editor and the panel meet now, and the divider straddles the boundary
    // they meet on rather than reserving a column of its own.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    MainComponent component (false);
    component.setSize (1400, 900);

    auto* panel = component.findChildWithID ("instrumentPanelViewport");
    auto* divider = component.findChildWithID ("panelDivider");

    REQUIRE (panel != nullptr);
    REQUIRE (divider != nullptr);

    // On the boundary, not beside it: there is no column of layout here.
    CHECK (divider->getX() < panel->getX());
    CHECK (divider->getRight() > panel->getX());

    const auto image = render (component);

    // Down the panel's own left gutter, well below the chevron. Exactly one
    // column brighter than the panel's ground - the seam - and the panel's own
    // former edge is not a second one.
    const auto y = component.getHeight() / 2;
    auto rules = 0;

    for (auto x = panel->getX(); x < panel->getX() + tokens::space::xl; ++x)
        if (image.getPixelAt (x, y).getBrightness()
            > tokens::colour::surface.getBrightness() + 0.02f)
            ++rules;

    INFO ("bright columns in the panel's left gutter: " << rules);
    CHECK (rules == 1);
}
