#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Settings.h"
#include "model/ProjectFactory.h"
#include "ui/DewDialog.h"
#include "ui/EffectCard.h"
#include "ui/EffectChainHost.h"
#include "ui/InstrumentPanel.h"
#include "ui/MainComponent.h"
#include "ui/MenuSeam.h"
#include "ui/PianoRollComponent.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"

#include <map>
#include <utility>
#include <vector>

#include "ControlWalkHarness.h"

using namespace dew;
using namespace dew::testing;

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

namespace
{

/** The panel's own knobs - the envelope and the levels. Its direct children,
    which is what tells them apart from the ones inside a face's section. */
std::vector<DewKnob*> panelKnobs (InstrumentPanel& panel)
{
    std::vector<DewKnob*> knobs;

    for (auto* child : panel.getChildren())
        if (auto* knob = dynamic_cast<DewKnob*> (child))
            if (knob->isVisible())
                knobs.push_back (knob);

    return knobs;
}

/** How many distinct rows those knobs are laid out on. */
int rowsUsedBy (const std::vector<DewKnob*>& knobs)
{
    std::vector<int> tops;

    for (auto* knob : knobs)
        if (std::find (tops.begin(), tops.end(), knob->getY()) == tops.end())
            tops.push_back (knob->getY());

    return (int) tops.size();
}

} // namespace

TEST_CASE ("the panel's knobs are one size, on however many rows they need", "[ui][reflow]")
{
    // The defect this ends: four envelope knobs divided the panel's width by
    // four and the two level knobs directly below them divided the same width
    // by two, so the same control was drawn at two sizes, one above the other,
    // at every panel width and in the panel that sits beside four others using
    // the same rung.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    EditorState editorState;
    document.setState (ProjectFactory::createDefault(), true);

    InstrumentPanel panel { document, editorState };

    // Driven at the panel's own width rather than the window's, because the
    // sidebar is dragged independently: Settings::minPanelWidth to
    // maxPanelWidth is the range a person can actually put it through, and both
    // ends of it have to work.
    for (const auto width :
         { Settings::maxPanelWidth, Settings::defaultPanelWidth, Settings::minPanelWidth })
    {
        panel.setSize (width, panel.getRequiredHeight());

        const auto knobs = panelKnobs (panel);

        INFO ("a " << width << "px panel put " << knobs.size() << " knobs on " << rowsUsedBy (knobs)
                   << " rows");

        // A synth channel: four envelope stages and two levels.
        REQUIRE (knobs.size() == 6);

        for (auto* knob : knobs)
        {
            CHECK (knob->getWidth() == knobs.front()->getWidth());

            // And nothing hangs off the edge, at any of them.
            CHECK (knob->getRight() <= panel.getWidth());
            CHECK (knob->getX() >= 0);
        }
    }

    // The two cases the grouping exists for, as a PAIR - one of them alone
    // would pass for a panel that never re-flowed at all. Wide: both groups on
    // one row, told apart by the rule the grid puts between them. Narrow: a row
    // each, told apart by the break itself.
    panel.setSize (Settings::maxPanelWidth, panel.getRequiredHeight());
    CHECK (rowsUsedBy (panelKnobs (panel)) == 1);

    panel.setSize (Settings::defaultPanelWidth, panel.getRequiredHeight());
    CHECK (rowsUsedBy (panelKnobs (panel)) == 2);
}

TEST_CASE ("nothing in the instrument panel is laid out past its own edge", "[ui][reflow]")
{
    // Every knob in the panel, the sections' included, at the smallest window
    // the app can open. A knob whose cell ran off the right edge would be a
    // control that could not be reached at all, and the panel scrolls
    // vertically only - there is nothing to scroll sideways into.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    MainComponent component (false);
    component.setSize (900, 560);

    auto* panel = dynamic_cast<InstrumentPanel*> (
        dynamic_cast<juce::Viewport*> (component.findChildWithID ("instrumentPanelViewport"))
            ->getViewedComponent());
    REQUIRE (panel != nullptr);

    auto seen = 0;

    const std::function<void (juce::Component&)> walk = [&] (juce::Component& root)
    {
        for (auto* child : root.getChildren())
        {
            if (child->isVisible())
            {
                if (dynamic_cast<DewKnob*> (child) != nullptr)
                {
                    ++seen;

                    const auto inPanel = panel->getLocalArea (child, child->getLocalBounds());

                    INFO ("knob at " << inPanel.toString() << " in a panel " << panel->getWidth()
                                     << "px wide");
                    CHECK (inPanel.getRight() <= panel->getWidth());
                    CHECK (inPanel.getX() >= 0);
                }

                walk (*child);
            }
        }
    };

    walk (*panel);

    // The control case: a walk that found nothing would pass in silence.
    INFO ("knobs walked: " << seen);
    CHECK (seen >= 6);
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

TEST_CASE ("every control is the height it says it needs", "[ui][reflow][design]")
{
    // What a person actually sees: a number field 40 tall inside an effect
    // card, 41 in the randomize dialog and 26 in a settings row, beside
    // dropdowns that are always 26. Every height in the application was decided
    // at the CALL SITE, so nothing anywhere related them.
    //
    // The size ladder's own gate could not see it: it fires on a `constexpr int
    // ...Height = <literal>` whose literal equals a rung, so 40, 41 and a bare
    // 12 all passed through it. This asks the question the ladder is FOR - and
    // it asks the CONTROL, because a captioned field and a bare one need
    // different answers and only the field knows which it is.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    MainComponent component (false);
    component.setSize (1400, 900);

    // With an effect on the first channel: the default project has none, so a
    // walk of it never reaches an effect card - which is where the fourteen
    // pixel gap was. A walk that cannot reach the defect it was written for is
    // not a walk.
    {
        auto project = component.getDocument().getState();
        auto channel = ProjectEdits::findChannel (project, 1);
        REQUIRE (channel.isValid());

        ProjectEdits::addEffect (project, channel, "filter",
                                 &component.getDocument().getUndoManager());
        component.documentWasReplaced();
    }

    component.setSize (1400, 900);
    component.resized();

    juce::StringArray wrong;
    auto checked = 0;
    auto fields = 0;

    for (int tab = 0; tab < Settings::numTabs; ++tab)
    {
        component.showTab (tab);
        component.resized();

        walk (component,
              [&] (juce::Component& c)
              {
                  if (! c.isVisible() || c.getHeight() <= 0)
                      return;

                  auto wanted = 0;

                  if (auto* field = dynamic_cast<DewNumberField*> (&c))
                  {
                      wanted = field->preferredHeight();
                      ++fields;
                  }
                  else if (auto* box = dynamic_cast<DewDropdown*> (&c))
                      wanted = box->preferredHeight();
                  else if (auto* button = dynamic_cast<DewButton*> (&c))
                      wanted = button->preferredHeight();
                  else
                      return;

                  ++checked;

                  // A strip shorter than a control squeezes it rather than
                  // clipping it: the playlist's "+ Automation" sits in the
                  // ruler's corner, and the ruler is 22 by the ladder. So a
                  // control SHORTER than controlHeight is taken as squeezed.
                  //
                  // What this cannot see is the rectangle a control was handed,
                  // only its parent - so a squeezed control in a strip that had
                  // room would pass. What it does catch is every control TALLER
                  // than it asked for, which is the whole reported defect: a
                  // number field at 40 and 41 beside dropdowns at 26.
                  if (c.getHeight() == wanted || c.getHeight() < tokens::size::controlHeight)
                      return;

                  wrong.add (describe (c) + "  is " + juce::String (c.getHeight()) + ", asked for "
                             + juce::String (wanted));
              });
    }

    // Control case: a walk that found no controls would report every one of
    // them well sized, and the ones that matter are the number FIELDS - the
    // only control here whose answer is not a constant.
    INFO ("controls checked: " << checked << ", of them number fields: " << fields);
    REQUIRE (checked > 20);
    REQUIRE (fields > 0);

    INFO ("controls not at the height they asked for:\n" << wrong.joinIntoString ("\n"));
    CHECK (wrong.isEmpty());
}

TEST_CASE ("a toolbar too narrow for its controls offers them instead of hiding them",
           "[ui][reflow][design]")
{
    // StripLayout hid a control there was no room for and said nothing, and its
    // own comment conceded that only setResizeLimits kept the transport bar out
    // of that case. The editor toolbars were not so lucky: UI scale multiplies
    // the PEER, so at 1.75x on a laptop the LOGICAL window is what the display
    // leaves rather than what anybody chose - and the piano roll's toolbar,
    // which wants about 870px, was silently dropping four groups.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    AudioEngine engine;
    EditorState editorState;

    document.setState (ProjectFactory::createDefault(), true);

    PianoRollComponent roll { document, engine, editorState };
    auto& toolbar = roll.getToolbar();

    const auto controlsIn = [&toolbar]
    {
        juce::StringArray names;

        for (auto* child : toolbar.getChildren())
            if (child->isVisible() && child->getComponentID() != "toolbarOverflow")
                names.add (dew::testing::describe (*child));

        return names;
    };

    roll.setSize (1400, 700);
    roll.resized();

    const auto wide = controlsIn();
    auto* overflowButton = toolbar.findChildWithID ("toolbarOverflow");

    REQUIRE (overflowButton != nullptr);
    CHECK_FALSE (overflowButton->isVisible());

    // Narrow enough that the strip cannot hold everything, which is what a
    // 900px window at 1.75x scale actually gives it.
    roll.setSize (620, 700);
    roll.resized();

    const auto narrow = controlsIn();

    INFO ("wide: " << wide.size() << " controls, narrow: " << narrow.size());
    REQUIRE (narrow.size() < wide.size());

    // The button is there, and its menu holds what the strip could not.
    CHECK (overflowButton->isVisible());

    const auto menu = toolbar.getOverflowMenu();
    const auto rows = menuItems (menu);

    // Every control that left the strip is represented. A GROUP counts for more
    // than one row - the zoom trio is one thing to a strip and three rows to a
    // person - which is why this is >= and not ==, and why the two widest
    // things on this toolbar used to vanish with nothing to show for them.
    INFO ("overflow menu:\n" << rows.joinIntoString ("\n"));
    CHECK (rows.size() >= wide.size() - narrow.size());

    // Nothing is a blank row: every label comes from the control's own tooltip,
    // so a control that says nothing about itself would show as an empty line
    // rather than be quietly unusable.
    for (const auto& row : rows)
        CHECK (row.trim().isNotEmpty());

    // And back: widening puts every control back on the strip and takes the
    // button away, so the menu is not a place things go and stay.
    roll.setSize (1400, 700);
    roll.resized();

    CHECK (controlsIn().size() == wide.size());
    CHECK_FALSE (overflowButton->isVisible());
}

TEST_CASE ("the sidebar is never narrower than the widest thing in it", "[ui][reflow]")
{
    /*  An effect card DECLARES the width it needs, and the sidebar never asked.

        EffectCard::cardMinWidth is "what the header packs: grip, bypass, icon,
        name, preset, reorder, remove and - vertically - the expand chevron",
        and Settings::minPanelWidth was a number chosen beside it rather than
        from it. At the old floor the header was about a dozen pixels over
        budget, and a rectangle emptied by removeFromLeft returns an empty one
        rather than complaining: what happened is that the NAME - laid out last,
        and the only thing on that header which is not a button - shrank to
        nothing, so a narrow sidebar showed a row of identical glyphs with no
        word saying which effect they belonged to.

        The two numbers live in libraries that cannot see each other: dew_app
        holds the panel's floor and dew_ui declares the card's. This is what
        keeps them agreeing, the way the cross-layer case over channelRamp keeps
        dew_model's copy of the colour ramp honest.
    */
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    EditorState editorState;
    document.setState (ProjectFactory::createDefault(), true);

    auto project = document.getState();
    auto channel = ProjectEdits::findChannel (project, 1);
    REQUIRE (channel.isValid());

    // A card to measure. The default project's channels carry no effects, and a
    // chain with nothing in it would pass this for the wrong reason.
    ProjectEdits::addEffect (project, channel, "filter", &document.getUndoManager());

    InstrumentPanel panel { document, editorState };

    // The narrowest the divider can be dragged to, less the scrollbar the panel
    // gives up when its content is taller than the window - which a sidebar
    // with an effect chain in it always is.
    panel.setSize (Settings::minPanelWidth - tokens::size::scrollThickness,
                   panel.getRequiredHeight());
    panel.resized();

    const std::function<EffectCard*(juce::Component&)> firstCard =
        [&] (juce::Component& root) -> EffectCard*
    {
        for (auto* child : root.getChildren())
        {
            if (auto* card = dynamic_cast<EffectCard*> (child))
                return card;

            if (auto* found = firstCard (*child))
                return found;
        }

        return nullptr;
    };

    auto* card = firstCard (panel);
    REQUIRE (card != nullptr);

    INFO ("a " << Settings::minPanelWidth << "px sidebar gives an effect card " << card->getWidth()
               << "px, and the card asks for " << EffectCard::cardMinWidth);

    CHECK (card->getWidth() >= EffectCard::cardMinWidth);
}
