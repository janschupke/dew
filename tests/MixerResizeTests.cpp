#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/ProjectDocument.h"
#include "model/ProjectFactory.h"
#include "ui/EditorState.h"
#include "ui/EffectChainHost.h"
#include "ui/MixerComponent.h"
#include "ui/design/Tokens.h"

#include "TestSupport.h"

using namespace dew;
using namespace dew::testing;

/*  Dragging the mixer's effect band, and what the extra height is for.

    Its own file rather than more of MixerUiTests.cpp, which is already close
    to the four hundred code lines the tree holds every file to.
*/

namespace
{

struct MixerHarness
{
    MixerHarness()
    {
        document.setState (ProjectFactory::createDefault(), true);
        mixer.setSize (1200, 700);
        mixer.setVisible (true);
        mixer.refresh();
        mixer.resized();
    }

    EffectChainHost& band()
    {
        auto* host = dynamic_cast<EffectChainHost*> (mixer.findChildWithID ("effectChainHost"));
        REQUIRE (host != nullptr);

        return *host;
    }

    ProjectDocument document;
    EditorState editorState;
    MixerComponent mixer { document, editorState };
};

/** A press or a drag at a fixed SCREEN y, expressed in whatever local frame the
    band has now.

    This is the situation the band's drag is written for and the reason it reads
    screen coordinates: growing the band moves its own top edge UP, under a
    pointer that has not moved. A test that kept translating a local point would
    be modelling a pointer that follows the thing it is dragging.
*/
juce::MouseEvent atScreenY (EffectChainHost& host, int screenY, bool dragged)
{
    const juce::Point<int> local { host.getWidth() / 2, screenY - host.getScreenPosition().y };

    return mouseEventAt (host, local.toFloat(), {}, 1, dragged);
}

/** The grab band, in the host's own coordinates. */
juce::Point<int> onGrip (EffectChainHost& host)
{
    return { host.getWidth() / 2, 0 };
}

} // namespace

TEST_CASE ("the effect band opens at one knob row and says so", "[mixer][ui][resize]")
{
    // The control case for everything below: nothing re-flows on upgrade, so a
    // mixer nobody has dragged is exactly the mixer that shipped.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    MixerHarness h;

    CHECK (h.mixer.getEffectBandRows() == tokens::size::effectBandRowsDefault);
    CHECK (h.band().getKnobRows() == tokens::size::effectBandRowsDefault);
    CHECK (h.band().getHeight() >= h.band().getPreferredHeight());
}

TEST_CASE ("the band's top edge is a grip, and says so before it is pressed", "[mixer][ui][resize]")
{
    // The pointer shape is the whole affordance: the band's top rule looked
    // exactly like the rule under the strips, and nothing said it could be
    // dragged.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    MixerHarness h;

    auto& band = h.band();

    CHECK (band.isOnResizeEdge (onGrip (band)));
    CHECK_FALSE (band.isOnResizeEdge ({ band.getWidth() / 2, band.getHeight() / 2 }));

    band.mouseMove (mouseEventAt (band, onGrip (band).toFloat(), {}, 1, false));
    CHECK (band.getMouseCursor() == juce::MouseCursor::UpDownResizeCursor);

    band.mouseMove (mouseEventAt (
        band, juce::Point<int> { band.getWidth() / 2, band.getHeight() / 2 }.toFloat(), {}, 1,
        false));
    CHECK (band.getMouseCursor() == juce::MouseCursor::NormalCursor);
}

TEST_CASE ("dragging the grip up makes the band deeper", "[mixer][ui][resize]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    MixerHarness h;

    auto& band = h.band();

    const auto before = h.mixer.getEffectBandRows();
    const auto grabbedAt = band.getScreenPosition().y;

    // What two more rows actually COST, asked of the thing that knows. A row is
    // not size::knobRow deep: a card spends space::sm between rows as well, so
    // the old drag - which divided the travel by knobRow and rounded - fell six
    // pixels behind the pointer for every rung it climbed, on top of only
    // moving in rungs at all.
    const auto twoMoreRows = band.bandHeightForRows (before + 2) - band.bandHeightForRows (before);

    band.mouseDown (atScreenY (band, grabbedAt, false));
    band.mouseDrag (atScreenY (band, grabbedAt - twoMoreRows, true));
    band.mouseUp (atScreenY (h.band(), grabbedAt - twoMoreRows, true));

    INFO ("was " << before << " rows, now " << h.mixer.getEffectBandRows());
    CHECK (h.mixer.getEffectBandRows() == before + 2);

    // The cards still stand on whole rows: the band may be any height, but the
    // budget it hands them is one a card can actually lay knobs out on.
    CHECK (h.band().getHeight() >= h.band().getPreferredHeight());
    CHECK (h.band().getKnobRows() == before + 2);
}

TEST_CASE ("the band follows the pointer between two knob rows", "[mixer][ui][resize]")
{
    /*  The defect this ends. The drag used to divide the pointer's travel by
        size::knobRow and round, so the one resizable thing in the application
        had four reachable positions 68px apart: half a rung of travel changed
        nothing at all, and then it jumped a whole one. Every other drag area in
        dew - the playlist's lane edge, the roll's velocity lane, the panel
        divider - follows the pointer.

        Asserted as a PAIR, because either half alone passes for the wrong
        reason: a band that moved by the whole travel would fail the second, and
        the old rounding drag passes the second while failing the first.
    */
    const juce::ScopedJuceInitialiser_GUI juceInit;
    MixerHarness h;

    auto& band = h.band();
    const auto grabbedAt = band.getScreenPosition().y;
    const auto before = h.band().getHeight();

    // A third of a row, which the old drag rounded away to nothing.
    const auto nudge = tokens::size::knobRow / 3;

    band.mouseDown (atScreenY (band, grabbedAt, false));
    band.mouseDrag (atScreenY (h.band(), grabbedAt - nudge, true));

    const auto nudged = h.band().getHeight();

    INFO ("a " << nudge << "px drag moved the band from " << before << " to " << nudged);
    CHECK (nudged == before + nudge);

    // And the cards have not gained a row for it: a third of a row of ground is
    // band ground, not a knob standing on part of a row.
    CHECK (h.band().getKnobRows() == tokens::size::effectBandRowsDefault);
}

TEST_CASE ("a resize drag is path-independent", "[mixer][ui][resize]")
{
    // Two routes to the same pointer position give the same band. A delta
    // summed between samples does not, which is the defect the chain's own
    // reorder is frozen against and the reason this reads from the press.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto rowsAfter = [] (const std::vector<int>& route)
    {
        MixerHarness h;
        auto& band = h.band();
        const auto grabbedAt = band.getScreenPosition().y;

        band.mouseDown (atScreenY (band, grabbedAt, false));

        for (const auto offset : route)
            band.mouseDrag (atScreenY (h.band(), grabbedAt + offset, true));

        band.mouseUp (atScreenY (h.band(), grabbedAt + route.back(), true));

        return h.mixer.getEffectBandRows();
    };

    const auto target = -2 * tokens::size::knobRow;

    const auto direct = rowsAfter ({ target });
    const auto wandering = rowsAfter ({ -20, -300, 40, -120, target });

    INFO ("direct " << direct << ", wandering " << wandering);
    CHECK (direct == wandering);
}

TEST_CASE ("the band clamps at both ends of its range", "[mixer][ui][resize]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    MixerHarness h;

    // On the HEIGHT, which is what is clamped. How many rows that height then
    // shows is a second question with a second answer - the band never takes
    // more than half the mixer, so a short window shows fewer rows than the
    // height it is holding would otherwise buy.
    h.mixer.setEffectBandRows (1000);
    CHECK (h.mixer.getEffectBandHeight()
           == h.band().bandHeightForRows (tokens::size::effectBandRowsMax));

    h.mixer.setEffectBandRows (0);
    CHECK (h.mixer.getEffectBandHeight()
           == h.band().bandHeightForRows (tokens::size::effectBandRowsMin));
}

TEST_CASE ("the strips keep half the mixer however deep the band is asked to be",
           "[mixer][ui][resize]")
{
    // A fader shorter than a thumb is not a fader. The band takes the rows it
    // is asked for and no more than half the panel, so a short window shows
    // fewer rows rather than a mixer with no strips in it.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    MixerHarness h;

    h.mixer.setEffectBandRows (tokens::size::effectBandRowsMax);

    for (const auto height : { 560, 700, 1080 })
    {
        h.mixer.setSize (1200, height);

        INFO ("at " << height << "px the band is " << h.band().getHeight());
        CHECK (h.band().getHeight() <= height / 2 + tokens::space::md);
        CHECK (h.band().getY() > 0);

        // Whatever it came to, the band shows exactly the rows it has room for
        // and is tall enough for them.
        CHECK (h.band().getHeight() >= h.band().getPreferredHeight());
    }
}

TEST_CASE ("a deeper band makes the cards narrower", "[mixer][ui][resize]")
{
    // What the extra height is FOR: the same chain fits further across, which
    // is the whole reason a row of cards has a height worth dragging.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    MixerHarness h;

    auto& chain = h.band().getChain();

    for (const auto* type : { "compressor", "phaser", "eq" })
        chain.addEffectOfType (type);

    h.editorState.dispatchPendingMessages();
    h.mixer.resized();

    REQUIRE (chain.getNumSlotRows() == 3);

    const auto atOneRow = chain.getRequiredWidth();

    h.mixer.setEffectBandRows (2);
    h.editorState.dispatchPendingMessages();

    const auto atTwoRows = chain.getRequiredWidth();

    INFO ("one row " << atOneRow << "px, two rows " << atTwoRows << "px");
    CHECK (h.band().getKnobRows() == 2);
    CHECK (atTwoRows < atOneRow);
}

TEST_CASE ("the sidebar's chain has no grip", "[mixer][ui][resize]")
{
    // A column is scrolled by whoever holds it and folds a card away instead,
    // so there is no band height there to spend - and an edge that answered the
    // pointer but did nothing would be worse than no edge at all.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    EditorState editorState;
    document.setState (ProjectFactory::createDefault(), true);

    EffectChainHost column { document, editorState, EffectChainHost::Orientation::vertical };
    column.setSize (300, 400);

    CHECK_FALSE (column.isOnResizeEdge ({ column.getWidth() / 2, 0 }));

    column.mouseMove (mouseEventAt (column, juce::Point<int> { column.getWidth() / 2, 0 }.toFloat(),
                                    {}, 1, false));
    CHECK (column.getMouseCursor() == juce::MouseCursor::NormalCursor);
}
