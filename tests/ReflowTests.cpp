#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/ProjectFactory.h"
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

TEST_CASE ("the sidebar is the only thing that scrolls in it", "[ui][reflow]")
{
    // The effect chain used to carry a Viewport of its own in BOTH
    // orientations, so in the sidebar it was a scroller nested inside the
    // scroller that already held the panel. Opening a card grew the inner one,
    // the panel kept the height it had been given, and the sidebar's own bar
    // never came up - which is to say an opened card at the bottom of a full
    // chain could not be reached at all.
    //
    // A ROW still scrolls: the mixer's chain is wider than the band it sits in
    // and has nothing outside it to do the job.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto viewportsIn = [] (juce::Component& root)
    {
        auto count = 0;

        for (auto* child : root.getChildren())
            if (dynamic_cast<juce::Viewport*> (child) != nullptr)
                ++count;

        return count;
    };

    MainComponent component (false);
    component.setSize (1400, 900);

    auto* sidebarChain = dynamic_cast<EffectChainHost*> (
        findDescendantWithID (component, "effectChainHost"));

    REQUIRE (sidebarChain != nullptr);
    CHECK (viewportsIn (*sidebarChain) == 0);

    // The mixer's, which is the same class the other way round.
    ProjectDocument document;
    EditorState editorState;
    document.setState (ProjectFactory::createDefault(), true);

    EffectChainHost row { document, editorState, EffectChainHost::Orientation::horizontal };
    row.setSize (600, 200);

    CHECK (viewportsIn (row) == 1);
}

TEST_CASE ("opening every card scrolls the sidebar rather than clipping it", "[ui][reflow]")
{
    // What the nested viewport hid. At a roomy window size the panel is exactly
    // its viewport and nothing scrolls; open every card in a full chain and the
    // panel has to grow past it and the bar has to come up.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    MainComponent component (false);
    component.setSize (1400, 900);

    auto* viewport = dynamic_cast<juce::Viewport*> (
        component.findChildWithID ("instrumentPanelViewport"));
    REQUIRE (viewport != nullptr);

    auto* panel = dynamic_cast<InstrumentPanel*> (viewport->getViewedComponent());
    REQUIRE (panel != nullptr);

    auto* host = dynamic_cast<EffectChainHost*> (
        findDescendantWithID (component, "effectChainHost"));
    REQUIRE (host != nullptr);

    auto& chain = host->getChain();

    for (const auto* type : { "filter", "delay", "reverb", "chorus" })
        chain.addEffectOfType (type);

    REQUIRE (chain.getNumSlotRows() == 4);

    for (int i = 0; i < chain.getNumSlotRows(); ++i)
        chain.setSlotExpanded (i, true);

    // Which cards are open is EditorState, and EditorState is a
    // ChangeBroadcaster: the write is immediate, the notification is a message.
    // Nothing has laid out again until it is delivered.
    component.getEditorState().dispatchPendingMessages();

    INFO ("panel " << panel->getHeight() << "px, viewport " << viewport->getMaximumVisibleHeight()
                   << "px");

    CHECK (panel->getHeight() == panel->getRequiredHeight());
    CHECK (panel->getHeight() > viewport->getMaximumVisibleHeight());
    CHECK (viewport->getVerticalScrollBar().isVisible());

    // And the band is as tall as the cards in it, not "whatever was left".
    CHECK (host->getHeight() == host->getPreferredHeight());

    // Closing them all again puts the panel back inside its viewport.
    for (int i = 0; i < chain.getNumSlotRows(); ++i)
        chain.setSlotExpanded (i, false);

    component.getEditorState().dispatchPendingMessages();

    CHECK (panel->getHeight() == viewport->getMaximumVisibleHeight());
    CHECK_FALSE (viewport->getVerticalScrollBar().isVisible());
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
