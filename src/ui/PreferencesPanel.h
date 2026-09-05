#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Settings.h"
#include "control/McpServer.h"
#include "ui/McpGrants.h"
#include "ui/PreferencesCatalog.h"
#include "ui/PreferencesPages.h"
#include "ui/primitives/DewControls.h"
#include "ui/primitives/DewSearchField.h"

namespace dew
{

class AudioEngine;
class LiveAudioHost;
class MidiInputHost;

/** Everything dew is configured by, in one window you can search.

    A sidebar of pages and a box that filters them. Typing does BOTH of the
    things the two editors people arrive from do: the sidebar narrows to the
    pages that hold a match, the way IntelliJ's tree does, and the content pane
    becomes one flat list of every match across every page, the way VS Code's
    does. Clearing the box puts the chosen page back.

    A result row is NAVIGATIONAL - it names the page, the setting and what the
    setting does, and choosing it opens that page with the row marked. It does
    not carry the live control, because only the pages built from the catalog
    could offer one and a list where half the rows were live and half were not
    reads worse than a list where none are.

    It REPLACES nothing. Audio Settings, MIDI Settings and MCP still open from
    the Audio menu and are still those panels; this window embeds the very same
    components, and its Appearance page invokes the very same commands the View
    menu does. There is one implementation of each setting and two ways in.
*/
class PreferencesPanel : public juce::Component
{
public:
    /** What the window needs from the application around it.

        An aggregate rather than eight constructor parameters, and every device
        member is a pointer: dew_shot and the tests build this with no audio
        host and no endpoint, and a window that could not be constructed without
        one could not be rendered or walked.
    */
    struct Hosts
    {
        LiveAudioHost* audio = nullptr;
        AudioEngine* engine = nullptr;
        MidiInputHost* midi = nullptr;

        /** ASKED FOR each time rather than held, because turning the MCP switch
            off destroys the endpoint - the contract McpConnectionsPanel
            documents, passed straight through. */
        std::function<control::McpServer*()> mcpServer;
        McpGrants* grants = nullptr;
        std::function<void()> onMcpEnabledChanged;

        /** The application's own chooseLanguage. Index 0 is "follow the
            system"; the rest index availableLocales(). */
        std::function<void (int)> onLanguageChosen;

        /** May be null, in which case the appearance controls show the right
            values and changing one does nothing. */
        juce::ApplicationCommandManager* commands = nullptr;
    };

    PreferencesPanel (Settings&, Hosts);
    ~PreferencesPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Opens it over `parent`, with dew's dialog conventions. */
    static void show (Settings&, Hosts, juce::Component* parent);

    // --- for tests -----------------------------------------------------------
    /** Applies a query, exactly as typing one does.

        Public and separate from the search field's callback so the filtering
        can be driven with no message loop - the seam every other panel here
        offers as a buildMenu/applyMenuChoice pair.
    */
    void setFilter (const juce::String& query);

    juce::String getFilter() const;

    /** The pages the sidebar is currently offering, in order. */
    std::vector<prefs::Page> getVisiblePages() const;

    /** The flat result list, or empty when no query is active. */
    std::vector<const prefs::Entry*> getResults() const;

    prefs::Page getSelectedPage() const noexcept
    {
        return selected;
    }
    void selectPage (prefs::Page);

    DewSearchField& getSearchField() noexcept
    {
        return search;
    }

    /** The category column, which parents a button per page rather than rows a
        model draws - so a test asserting on them has something to walk. */
    juce::Component& getSidebarForTesting() noexcept
    {
        return sidebar;
    }

    /** The page component in front, so a test can reach an embedded panel. */
    PreferencesPage* getPageComponent (prefs::Page) const;

    static constexpr int preferredWidth = 720;
    static constexpr int preferredHeight = 480;

private:
    /** One page in the sidebar. A ghost button rather than a ListBox row, which
        is what every other multi-row surface in dew is built from - and which
        brings a tooltip, a focus ring and the keyboard with it. */
    struct Category
    {
        prefs::Page page;
        std::unique_ptr<DewButton> button;
    };

    /** One match, in the flat list a query puts in the content pane. */
    struct Result
    {
        const prefs::Entry* entry = nullptr;
        std::unique_ptr<DewButton> button;
    };

    void buildCategories();
    void buildPages (Hosts&);
    void rebuildResults();
    void updateVisibility();

    /** Stacks the result rows and sizes the list they scroll inside. */
    void layOutResults (int width);

    /** Opens the page `entry` lives on and marks its row. */
    void revealEntry (const prefs::Entry& entry);

    Settings& settings;

    DewSearchField search;
    juce::String filter;

    juce::Viewport sidebarViewport;
    juce::Component sidebar;
    std::vector<Category> categories;

    juce::Viewport contentViewport;
    juce::Component content;
    std::vector<std::pair<prefs::Page, std::unique_ptr<PreferencesPage>>> pages;

    juce::Viewport resultsViewport;
    juce::Component results;
    std::vector<Result> resultRows;

    prefs::Page selected = prefs::Page::appearance;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PreferencesPanel)
};

} // namespace dew
