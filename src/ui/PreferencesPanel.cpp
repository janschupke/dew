// =============================================================================
// The preferences window: a sidebar, a search box, and whichever page is in
// front.
//
// The filtering half - what a query does to the sidebar and to the content
// pane - is next door in PreferencesSearch.cpp, for the reason PianoRoll is
// five files: no file is over 400 lines of code, and a gate says so.
// =============================================================================

#include "ui/PreferencesPanel.h"

#include "engine/AudioEngine.h"
#include "i18n/Strings.h"
#include "io/LiveAudioHost.h"
#include "io/MidiInputHost.h"
#include "ui/AudioSettingsPanel.h"
#include "ui/DewDialog.h"
#include "ui/McpConnectionsPanel.h"
#include "ui/MidiSettingsPanel.h"
#include "ui/design/Tokens.h"

namespace dew
{

using namespace tokens;

namespace
{

/** The category column. Not a rung of the size ladder: it is as wide as the
    longest page name at body size plus the room a ghost button needs, and
    nothing else in dew is that wide for that reason. */
constexpr int sidebarWidth = 190;

/** One group, so choosing a page unchooses the last one without this having to
    say which that was. */
constexpr int categoryRadioGroup = 1;

} // namespace

PreferencesPanel::PreferencesPanel (Settings& s, Hosts hosts)
    : settings (s)
{
    setComponentID ("preferences");

    // The pair every panel in dew sets: a name a screen reader reads, and a
    // PLACE in the tree. focusContainer, never keyboardFocusContainer - the
    // second confines tab to this component with no key to leave it.
    setTitle (tr (StringId::preferences_title));
    setFocusContainerType (FocusContainerType::focusContainer);

    search.setPlaceholder (tr (StringId::preferences_search_placeholder));
    search.setTooltip (tr (StringId::preferences_search_help));
    search.setClearTooltip (tr (StringId::preferences_search_clear));
    search.onTextChange = [this] (const juce::String& query) { setFilter (query); };
    addAndMakeVisible (search);

    sidebarViewport.setViewedComponent (&sidebar, false);
    sidebarViewport.setScrollBarsShown (true, false);
    sidebarViewport.setComponentID ("preferencesSidebar");
    addAndMakeVisible (sidebarViewport);

    contentViewport.setViewedComponent (&content, false);
    contentViewport.setScrollBarsShown (true, false);
    contentViewport.setComponentID ("preferencesContent");
    addAndMakeVisible (contentViewport);

    resultsViewport.setViewedComponent (&results, false);
    resultsViewport.setScrollBarsShown (true, false);
    resultsViewport.setComponentID ("preferencesResults");
    addChildComponent (resultsViewport);

    // Pages first: a category is a way to REACH a page, so one whose page was
    // not built - a device host this window was given none of - would be a row
    // that selects nothing. dew_shot and the tests are where that happens.
    buildPages (hosts);
    buildCategories();

    selectPage (prefs::Page::appearance);

    setSize (preferredWidth, preferredHeight);
}

PreferencesPanel::~PreferencesPanel() = default;

void PreferencesPanel::buildCategories()
{
    for (const auto page : prefs::pageOrder())
    {
        if (getPageComponent (page) == nullptr)
            continue;

        auto button = std::make_unique<DewButton> (tr (prefs::titleOf (page)),
                                                   DewButton::Role::ghost);

        button->setClickingTogglesState (true);
        button->setTextJustification (juce::Justification::centredLeft);
        button->setRadioGroupId (categoryRadioGroup);
        button->setTooltip (tr (prefs::titleOf (page)));
        button->onClick = [this, page] { selectPage (page); };

        sidebar.addAndMakeVisible (*button);
        categories.push_back ({ page, std::move (button) });
    }
}

void PreferencesPanel::buildPages (Hosts& hosts)
{
    const auto add = [this] (prefs::Page page, std::unique_ptr<PreferencesPage> component)
    {
        content.addChildComponent (*component);
        pages.emplace_back (page, std::move (component));
    };

    add (prefs::Page::appearance,
         std::make_unique<AppearancePage> (settings, hosts.commands,
                                           std::move (hosts.onLanguageChosen)));

    // The three that are the panels dew already had, constructed exactly as
    // MainComponent's own showAudioSettings / showMidiSettings / showMcpSettings
    // construct them. A null host means the page is empty rather than absent,
    // which is what dew_shot and the tests get.
    if (hosts.audio != nullptr && hosts.engine != nullptr)
        add (prefs::Page::audio,
             std::make_unique<HostedPage> (
                 std::make_unique<AudioSettingsPanel> (*hosts.audio, *hosts.engine),
                 AudioSettingsPanel::preferredWidth, AudioSettingsPanel::preferredHeight));

    if (hosts.midi != nullptr)
        add (prefs::Page::midi,
             std::make_unique<HostedPage> (
                 std::make_unique<MidiSettingsPanel> (*hosts.midi, &settings),
                 MidiSettingsPanel::preferredWidth, MidiSettingsPanel::preferredHeight));

    add (prefs::Page::rendering, std::make_unique<RenderingPage> (settings));

    add (prefs::Page::connections,
         std::make_unique<HostedPage> (
             std::make_unique<McpConnectionsPanel> (hosts.mcpServer, hosts.grants, &settings,
                                                    hosts.onMcpEnabledChanged),
             McpConnectionsPanel::preferredWidth, McpConnectionsPanel::preferredHeight));
}

PreferencesPage* PreferencesPanel::getPageComponent (prefs::Page page) const
{
    for (const auto& entry : pages)
        if (entry.first == page)
            return entry.second.get();

    return nullptr;
}

void PreferencesPanel::selectPage (prefs::Page page)
{
    selected = page;

    for (const auto& entry : pages)
        entry.second->setVisible (entry.first == page);

    for (auto& category : categories)
    {
        const auto chosen = category.page == page;

        category.button->setToggleState (chosen, juce::dontSendNotification);

        // The ROLE carries the selection, because DewButton::Role::ghost paints
        // the same whether it is toggled or not - it is a list row, and a list
        // row that reads as pressed is a button. `normal` toggled is the accent
        // fill the design system already uses for "this one".
        category.button->setRole (chosen ? DewButton::Role::normal : DewButton::Role::ghost);
    }

    resized();
}

void PreferencesPanel::show (Settings& settings, Hosts hosts, juce::Component* parent)
{
    dialog::launch (new PreferencesPanel (settings, std::move (hosts)),
                    tr (StringId::preferences_title), parent);
}

void PreferencesPanel::paint (juce::Graphics& g)
{
    g.fillAll (colour::background);

    // The sidebar sits on its own ground, which is what makes it read as a
    // list of places rather than as four buttons floating beside the content.
    auto area = getLocalBounds().reduced (space::xl);
    area.removeFromTop (size::controlHeight + space::lg);

    paint::surface (g, area.removeFromLeft (sidebarWidth), colour::surface);

    g.setColour (colour::divider);
    g.fillRect (area.removeFromLeft (stroke::hairlinePx));

    // Said rather than left blank. A window that answers a query with an empty
    // pane has not said whether it searched.
    if (filter.isNotEmpty() && resultRows.empty())
    {
        area.removeFromLeft (space::lg);
        paint::emptyState (
            g, area,
            tr (StringId::preferences_search_empty, Args {}.with ("query", filter.trim())));
    }
}

void PreferencesPanel::resized()
{
    auto area = getLocalBounds().reduced (space::xl);

    search.setBounds (area.removeFromTop (size::controlHeight));
    area.removeFromTop (space::lg);

    sidebarViewport.setBounds (area.removeFromLeft (sidebarWidth));
    area.removeFromLeft (stroke::hairlinePx + space::lg);

    auto row = juce::Rectangle<int> (sidebarWidth, size::controlHeight);
    auto y = space::sm;

    for (auto& category : categories)
    {
        if (! category.button->isVisible())
            continue;

        category.button->setBounds (row.withY (y).reduced (space::sm, 0));
        y += size::controlHeight + space::xxs;
    }

    sidebar.setSize (sidebarViewport.getWidth(), juce::jmax (sidebarViewport.getHeight(), y));

    resultsViewport.setBounds (area);
    contentViewport.setBounds (area);

    if (auto* page = getPageComponent (selected))
    {
        content.setSize (contentViewport.getWidth(),
                         juce::jmax (contentViewport.getHeight(), page->getRequiredHeight()));
        page->setBounds (content.getLocalBounds());
    }

    layOutResults (area.getWidth());
}

} // namespace dew
