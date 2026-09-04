#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/DewDialog.h"
#include "ui/EffectChainHost.h"
#include "ui/InstrumentPanel.h"
#include "ui/MainComponent.h"

using namespace dew;

/*  What happens when the window is too small for what is in it.

    WCAG 1.4.10 asks that content reflow rather than be lost. dew's timelines
    are exempt - a piano roll genuinely needs two dimensions - but the panels
    and the dialogs are not, and both used to clip in silence.

    This matters at ordinary window sizes because UI scale multiplies the peer:
    at 1.75x a 900x560 window is asking for 1575x980 of screen, so the LOGICAL
    size the layout sees is what a laptop leaves, not what the user chose.
*/

namespace
{

juce::Component* findDescendantWithID (juce::Component& root, const juce::String& id)
{
    for (auto* child : root.getChildren())
    {
        if (child->getComponentID() == id)
            return child;

        if (auto* found = findDescendantWithID (*child, id))
            return found;
    }

    return nullptr;
}

} // namespace

TEST_CASE ("the effect chain survives the smallest window the app can open", "[ui][reflow]")
{
    // 900x560 is setResizeLimits' own floor, so this is not a hypothetical
    // size - it is the smallest thing a user can drag the window to. The chain
    // used to be handed `area` after four fixed rows had been removed from it,
    // which at this height is empty: the chain vanished and nothing said so.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    MainComponent component (false);
    component.setSize (900, 560);

    auto* host = dynamic_cast<EffectChainHost*> (
        findDescendantWithID (component, "effectChainHost"));
    REQUIRE (host != nullptr);

    INFO ("chain host " << host->getHeight() << "px, wants " << host->getPreferredHeight());
    CHECK (host->getHeight() >= host->getPreferredHeight());
}

TEST_CASE ("a panel too tall for its viewport scrolls instead of clipping", "[ui][reflow]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    MainComponent component (false);

    auto* viewport = dynamic_cast<juce::Viewport*> (
        component.findChildWithID ("instrumentPanelViewport"));
    REQUIRE (viewport != nullptr);

    auto* panel = dynamic_cast<InstrumentPanel*> (viewport->getViewedComponent());
    REQUIRE (panel != nullptr);

    // Roomy: the panel is exactly the viewport, and no scrollbar appears. This
    // is the control case - a viewport that always scrolled would pass the
    // assertion below while having changed the app at every size.
    component.setSize (1400, 900);
    CHECK (panel->getHeight() == viewport->getMaximumVisibleHeight());

    // isVerticalScrollBarShown() is the ENABLED flag, not the visible one -
    // it answers setScrollBarsShown, not "is there a bar down the panel".
    CHECK_FALSE (viewport->getVerticalScrollBar().isVisible());

    // Cramped: the panel keeps the height it needs and the viewport scrolls it.
    component.setSize (900, 560);
    CHECK (panel->getHeight() == panel->getRequiredHeight());
    CHECK (panel->getHeight() > viewport->getMaximumVisibleHeight());
    CHECK (viewport->getVerticalScrollBar().isVisible());
}

TEST_CASE ("a dialog is never taller than the screen it opens on", "[ui][reflow]")
{
    // The render panel grows itself as rows appear, and dew's dialogs are not
    // resizable by convention. Together those put the Render button below the
    // bottom edge of a short display, on a window that could not be resized to
    // bring it back.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto tallest = dialog::maxContentHeight();

    INFO ("tallest dialog content: " << tallest);
    CHECK (tallest > 0);

    // Every dialog dew ships declares a preferred height. None may exceed the
    // screen; the ones that would are scrolled by dialog::launch instead.
    const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();

    if (display != nullptr)
        CHECK (tallest <= juce::roundToInt (display->userBounds.getHeight()));
}
