#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Settings.h"
#include "engine/AudioEngine.h"
#include "io/LiveAudioHost.h"
#include "io/MidiInputHost.h"
#include "ui/PreferencesCatalog.h"
#include "ui/PreferencesPanel.h"
#include "ui/design/Theme.h"
#include "ControlWalkHarness.h"
#include "PaintProbe.h"
#include "TestSupport.h"

using namespace dew;
using namespace dew::testing;

namespace
{

/** findChildWithID is NOT recursive, and every row in this window is at least
    two levels down - inside a viewport, inside a page. */
juce::Component* findDescendantWithID (juce::Component& parent, const juce::String& id)
{
    for (auto* child : parent.getChildren())
    {
        if (child->getComponentID() == id)
            return child;

        if (auto* found = findDescendantWithID (*child, id))
            return found;
    }

    return nullptr;
}

/** The devices a whole window needs, kept alive beside the panel that borrows
    them. Neither is started: a test must not take over the sound card, and the
    panels are honest about there being no device, which is the case worth
    asserting on anyway.
*/
struct Rig
{
    Rig (Settings& settings, juce::ApplicationCommandManager* commands = nullptr)
    {
        PreferencesPanel::Hosts hosts;
        hosts.audio = &host;
        hosts.engine = &engine;
        hosts.midi = &midiHost;
        hosts.commands = commands;

        panel = std::make_unique<PreferencesPanel> (settings, std::move (hosts));
        panel->setSize (PreferencesPanel::preferredWidth, PreferencesPanel::preferredHeight);
        panel->setVisible (true);
        panel->resized();
    }

    AudioEngine engine;
    LiveAudioHost host { engine };
    MidiInputHost midiHost { host.getDeviceManager(), engine };

    std::unique_ptr<PreferencesPanel> panel;
};

} // namespace

TEST_CASE ("every page is named and every entry says something", "[ui][preferences]")
{
    // The control case first: a catalog walk over an empty table would make
    // every assertion below vacuous, which is the failure mode every gate in
    // this tree exists to avoid.
    REQUIRE (prefs::entries().size() > 10);
    REQUIRE (prefs::pageOrder().size() == 5);

    for (const auto page : prefs::pageOrder())
    {
        INFO ("page " << (int) page);

        // tr() never returns empty - a missing row answers with its own dotted
        // key - so a blank here would mean a key that is not in the catalogue
        // at all, which is a compile error, not this.
        REQUIRE (tr (prefs::titleOf (page)).isNotEmpty());

        auto entriesOnPage = 0;

        for (const auto& entry : prefs::entries())
            if (entry.page == page)
                ++entriesOnPage;

        // A page with no entries is a page search can never find.
        INFO ("entries: " << entriesOnPage);
        REQUIRE (entriesOnPage > 0);
    }

    for (const auto& entry : prefs::entries())
    {
        INFO (keyOf (entry.title));
        REQUIRE (tr (entry.title).isNotEmpty());
        REQUIRE (tr (entry.description).isNotEmpty());

        // The dotted key back, which is what a missing row answers with: a row
        // whose text IS its key was never translated.
        REQUIRE (tr (entry.title) != keyOf (entry.title));
        REQUIRE (tr (entry.description) != keyOf (entry.description));
    }
}

TEST_CASE ("search matches a name, an explanation and a value label", "[ui][preferences]")
{
    // An empty query is "not searching" rather than "nothing matched".
    REQUIRE (prefs::search ({}).size() == prefs::entries().size());
    REQUIRE (prefs::search ("   ").size() == prefs::entries().size());

    // By title.
    const auto byTitle = prefs::search ("theme");
    REQUIRE (byTitle.size() == 1);
    CHECK (byTitle.front()->page == prefs::Page::appearance);

    // By a VALUE label. "High contrast" appears in none of the theme row's own
    // text - it is what the choice is called - so this is the half of the index
    // that a title-and-description search would miss.
    const auto byChoice = prefs::search ("high contrast");
    REQUIRE (byChoice.size() == 1);
    CHECK (byChoice.front()->page == prefs::Page::appearance);

    // By the page's own name, which is how a whole category is listed.
    CHECK (prefs::search ("rendering").size() == 5);

    // Case-insensitive, and trimmed.
    CHECK (prefs::search ("THEME").size() == 1);
    CHECK (prefs::search ("  theme  ").size() == 1);

    // A query nothing matches finds nothing rather than everything.
    CHECK (prefs::search ("zzzznotasetting").empty());

    // The page filter agrees with the entry filter.
    CHECK (prefs::pageMatches (prefs::Page::appearance, "theme"));
    CHECK_FALSE (prefs::pageMatches (prefs::Page::midi, "theme"));
    CHECK (prefs::pageMatches (prefs::Page::midi, {}));
}

TEST_CASE ("the preferences window paints and holds every page", "[ui][preferences]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const TempDir temp { "dew-prefs-" };

    Settings settings { temp.dir };
    Rig rig { settings };
    auto& panel = rig.panel;

    REQUIRE (inkCoverage (render (*panel)) > 0.0f);

    for (const auto page : prefs::pageOrder())
    {
        INFO ("page " << (int) page);
        REQUIRE (panel->getPageComponent (page) != nullptr);
    }

    // With no device hosts at all - what dew_shot builds - the two pages that
    // need one are absent rather than empty, and the sidebar reflects that
    // rather than offering a row that selects nothing.
    PreferencesPanel headless { settings, PreferencesPanel::Hosts {} };
    headless.setSize (PreferencesPanel::preferredWidth, PreferencesPanel::preferredHeight);
    headless.resized();

    CHECK (headless.getPageComponent (prefs::Page::audio) == nullptr);
    CHECK (headless.getPageComponent (prefs::Page::appearance) != nullptr);
    CHECK (headless.getVisiblePages().size() == 3);
    CHECK (inkCoverage (render (headless)) > 0.0f);

    REQUIRE (findDescendantWithID (*panel, "searchField") != nullptr);
    REQUIRE (findDescendantWithID (*panel, "preferencesAppearance") != nullptr);
    REQUIRE (findDescendantWithID (*panel, "preferencesRendering") != nullptr);

    CHECK (panel->getSelectedPage() == prefs::Page::appearance);
}

TEST_CASE ("a query narrows the sidebar and lists every match", "[ui][preferences]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const TempDir temp { "dew-prefs-" };

    Settings settings { temp.dir };
    Rig rig { settings };
    auto& panel = rig.panel;

    // Not searching: every page is offered and there is no result list.
    REQUIRE (panel->getVisiblePages().size() == prefs::pageOrder().size());
    REQUIRE (panel->getResults().empty());

    // Searching does BOTH: the sidebar narrows AND the content pane becomes the
    // flat cross-category list.
    panel->setFilter ("theme");

    CHECK (panel->getVisiblePages().size() == 1);
    CHECK (panel->getVisiblePages().front() == prefs::Page::appearance);
    CHECK (panel->getResults().size() == 1);

    // A query that spans two pages lists both, and offers both.
    panel->setFilter ("sample rate");
    CHECK (panel->getVisiblePages().size() == 2);
    CHECK (panel->getResults().size() == 2);

    // Nothing matched: no page offered, no result, and the window says so
    // rather than showing an empty pane - which is what it paints.
    panel->setFilter ("zzzznotasetting");
    CHECK (panel->getVisiblePages().empty());
    CHECK (panel->getResults().empty());
    CHECK (inkCoverage (render (*panel)) > 0.0f);

    // Cleared: back to where it started, exactly.
    panel->setFilter ({});
    CHECK (panel->getVisiblePages().size() == prefs::pageOrder().size());
    CHECK (panel->getResults().empty());
    CHECK (panel->getSearchField().getText().isEmpty());
}

TEST_CASE ("every control in the preferences window can be found, named and reached",
           "[ui][preferences][a11y]")
{
    // The four coverage gates, by hand: forEachControl walks MainComponent, and
    // a dialog panel is not in it - the same reason AudioSettingsPanel is not
    // covered either.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const TempDir temp { "dew-prefs-" };

    Settings settings { temp.dir };

    AudioEngine engine;
    LiveAudioHost host { engine };
    MidiInputHost midiHost { host.getDeviceManager(), engine };

    PreferencesPanel::Hosts hosts;
    hosts.audio = &host;
    hosts.engine = &engine;
    hosts.midi = &midiHost;

    PreferencesPanel panel { settings, std::move (hosts) };
    panel.setSize (PreferencesPanel::preferredWidth, PreferencesPanel::preferredHeight);
    panel.setVisible (true);

    auto controls = 0;

    for (const auto page : prefs::pageOrder())
    {
        panel.selectPage (page);
        panel.resized();
    }

    panel.selectPage (prefs::Page::appearance);
    panel.resized();

    // The controls this window OWNS: its search field, its sidebar, and the two
    // pages built from the catalog.
    //
    // Deliberately NOT the hosted pages. AudioSettingsPanel and
    // MidiSettingsPanel are embedded whole and unchanged, and neither gives its
    // dropdowns a tooltip or a name - MidiSettingsPanel.cpp contains no
    // setTooltip call at all. That is pre-existing and predates this window:
    // AccessibilityTests walks MainComponent, those two panels have only ever
    // lived in dialogs of their own, so nothing has ever walked them. Embedding
    // them here is what made it visible. Widening this walk would report a
    // defect in files this change does not touch; fixing it is a separate piece
    // of work in those two panels, needing a catalogue key per control.
    juce::Component* owned[] { &panel.getSearchField(),
                               panel.getPageComponent (prefs::Page::appearance),
                               panel.getPageComponent (prefs::Page::rendering) };

    for (auto* root : owned)
    {
        REQUIRE (root != nullptr);

        walk (*root,
              [&] (juce::Component& c)
              {
                  if (! isAControl (c))
                      return;

                  ++controls;

                  INFO (describe (c));

                  // Says what it is, to the status line and to a screen reader,
                  // which are the same curated sentence.
                  if (auto* client = dynamic_cast<juce::SettableTooltipClient*> (&c))
                      CHECK (client->getTooltip().isNotEmpty());

                  // Title OR name, the rule AccessibilityTests holds the rest of
                  // the window to: juce::Button seeds its NAME from the
                  // constructor argument, and that is a real accessible name.
                  CHECK ((c.getTitle().isNotEmpty() || c.getName().isNotEmpty()));

                  // Reachable without a mouse. Disabled controls are exempt - an
                  // unavailable render format is one.
                  if (c.isEnabled())
                      CHECK (c.getWantsKeyboardFocus());
              });
    }

    // The sidebar too, which is a control per page rather than a list.
    walk (panel.getSidebarForTesting(),
          [&] (juce::Component& c)
          {
              if (! isAControl (c))
                  return;

              ++controls;
              INFO (describe (c));

              if (auto* client = dynamic_cast<juce::SettableTooltipClient*> (&c))
                  CHECK (client->getTooltip().isNotEmpty());

              CHECK ((c.getTitle().isNotEmpty() || c.getName().isNotEmpty()));
              CHECK (c.getWantsKeyboardFocus());
          });

    // The control case: four appearance rows, five rendering rows, the search
    // field's clear button and one sidebar row per page. A walk that found
    // almost nothing would pass for the wrong reason.
    INFO (controls << " controls walked");
    REQUIRE (controls > 12);
}

TEST_CASE ("the appearance page shows what is stored", "[ui][preferences]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const TempDir temp { "dew-prefs-" };

    theme::applyPalette (theme::Kind::dark);

    Settings settings { temp.dir };
    settings.setUiScale (Settings::uiScaleSteps[2]);
    settings.setMotionPreference (Settings::Motion::reduced);

    Rig rig { settings };
    auto& panel = rig.panel;
    auto* page = panel->getPageComponent (prefs::Page::appearance);

    REQUIRE (page != nullptr);

    // Read from Settings rather than from the command manager, which is what
    // makes the page right with no application registered - and what dew_shot
    // depends on.
    juce::Array<juce::ComboBox*> boxes;

    walk (*page,
          [&] (juce::Component& c)
          {
              if (auto* box = dynamic_cast<juce::ComboBox*> (&c))
                  boxes.add (box);
          });

    REQUIRE (boxes.size() == 4);

    // In catalog order: theme, interface size, motion, language.
    CHECK (boxes[0]->getSelectedId() == (int) theme::current() + 1);
    CHECK (boxes[1]->getSelectedId() == 3);
    CHECK (boxes[2]->getSelectedId() == (int) Settings::Motion::reduced + 1);
    CHECK (boxes[3]->getSelectedId() == 1);
}

TEST_CASE ("the rendering page writes what is chosen back to settings", "[ui][preferences]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const TempDir temp { "dew-prefs-" };

    Settings settings { temp.dir };
    settings.setRenderBitDepth (16);
    settings.setRenderNormalize (false);

    Rig rig { settings };
    auto& panel = rig.panel;
    auto* page = panel->getPageComponent (prefs::Page::rendering);

    REQUIRE (page != nullptr);

    // The stored value arrives on the control.
    juce::Array<juce::ComboBox*> boxes;
    juce::Array<juce::Button*> toggles;

    walk (*page,
          [&] (juce::Component& c)
          {
              if (auto* box = dynamic_cast<juce::ComboBox*> (&c))
                  boxes.add (box);
              else if (auto* toggle = dynamic_cast<juce::ToggleButton*> (&c))
                  toggles.add (toggle);
          });

    REQUIRE (boxes.size() == 3);
    REQUIRE (toggles.size() == 1);

    CHECK (boxes[2]->getSelectedId() == 16);
    CHECK_FALSE (toggles[0]->getToggleState());

    // And a change goes back. setSelectedId with sendNotification is what a
    // click does, and there is no message loop here for anything else to.
    boxes[2]->setSelectedId (24, juce::sendNotificationSync);
    CHECK (settings.getRenderBitDepth() == 24);

    toggles[0]->setToggleState (true, juce::sendNotificationSync);
    CHECK (settings.getRenderNormalize());
}

TEST_CASE ("choosing a result opens its page with the row marked", "[ui][preferences]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const TempDir temp { "dew-prefs-" };

    Settings settings { temp.dir };
    Rig rig { settings };
    auto& panel = rig.panel;

    panel->setFilter ("bit depth");

    const auto results = panel->getResults();
    REQUIRE (results.size() == 1);
    REQUIRE (results.front()->page == prefs::Page::rendering);

    // By ID, not by type: every hosted panel brings buttons of its own, and
    // Component::isVisible answers for the component's own flag rather than for
    // whether an ancestor is showing - so a type-and-visibility filter picks up
    // a revoke button on a page nobody is looking at.
    juce::Array<juce::Button*> rows;

    walk (*panel,
          [&] (juce::Component& c)
          {
              if (c.getComponentID() == "preferencesResult")
                  if (auto* button = dynamic_cast<juce::Button*> (&c))
                      rows.add (button);
          });

    REQUIRE (! rows.isEmpty());

    // onClick rather than triggerClick: Button::triggerClick POSTS a message,
    // and there is no message loop here - it is one of the four ways a headless
    // test passes while proving nothing.
    rows.getFirst()->onClick();

    // Clicking clears the query - so the page it opened is visible rather than
    // behind the list that named it.
    CHECK (panel->getFilter().isEmpty());
    CHECK (panel->getSelectedPage() == prefs::Page::rendering);
    CHECK (panel->getResults().empty());
}
