// =============================================================================
// What a query does: the sidebar narrows, and the content pane becomes a flat
// list of every match across every page.
//
// The same class, a second translation unit beside PreferencesPanel.cpp. Split
// for the size gate, along the seam that was already there - everything here is
// driven by `filter` and nothing else in the window is.
// =============================================================================

#include "ui/PreferencesPanel.h"

#include "i18n/Strings.h"
#include "ui/design/Tokens.h"

namespace dew
{

using namespace tokens;

void PreferencesPanel::setFilter (const juce::String& query)
{
    filter = query;

    // The field is set as well as read, so setFilter is the whole seam: a test
    // drives this and gets exactly what typing would have produced, with no
    // message loop to pump. It is silent when the text already matches, so this
    // cannot recurse through onTextChange.
    if (search.getText() != query)
        search.setText (query, juce::dontSendNotification);

    rebuildResults();
    updateVisibility();
    resized();
    repaint();
}

juce::String PreferencesPanel::getFilter() const
{
    return filter;
}

std::vector<prefs::Page> PreferencesPanel::getVisiblePages() const
{
    std::vector<prefs::Page> visible;

    for (const auto& category : categories)
        if (category.button->isVisible())
            visible.push_back (category.page);

    return visible;
}

std::vector<const prefs::Entry*> PreferencesPanel::getResults() const
{
    std::vector<const prefs::Entry*> found;

    for (const auto& row : resultRows)
        found.push_back (row.entry);

    return found;
}

void PreferencesPanel::rebuildResults()
{
    resultRows.clear();
    results.removeAllChildren();

    // An empty query is not "no matches", it is "not searching" - the content
    // pane goes back to showing the chosen page, so there is no list to build.
    if (filter.trim().isEmpty())
        return;

    for (const auto* entry : prefs::search (filter))
    {
        auto button = std::make_unique<DewButton> (
            tr (StringId::preferences_results_row,
                Args {}
                    .with ("page", tr (prefs::titleOf (entry->page)))
                    .with ("setting", tr (entry->title))),
            DewButton::Role::ghost);

        // What the setting DOES, which is the sentence the status line shows
        // and the one a screen reader reads - so a result says more than its
        // own two words without the row having to hold three lines.
        button->setComponentID ("preferencesResult");

        // A list row, not a button: centred text here reads as two labels
        // floating in an empty pane rather than as a list of results.
        button->setTextJustification (juce::Justification::centredLeft);
        button->setTooltip (tr (entry->description));
        button->onClick = [this, entry] { revealEntry (*entry); };

        results.addAndMakeVisible (*button);
        resultRows.push_back ({ entry, std::move (button) });
    }
}

void PreferencesPanel::updateVisibility()
{
    const auto searching = filter.trim().isNotEmpty();

    for (auto& category : categories)
        category.button->setVisible (prefs::pageMatches (category.page, filter));

    // One or the other, never both: the flat list REPLACES the page while a
    // query is active, which is what makes clearing the box put the page back
    // exactly as it was.
    contentViewport.setVisible (! searching);
    resultsViewport.setVisible (searching);
}

void PreferencesPanel::layOutResults (int width)
{
    auto y = 0;

    for (auto& row : resultRows)
    {
        row.button->setBounds (0, y, width, size::stripFormRow);
        y += size::stripFormRow + space::xxs;
    }

    results.setSize (width, juce::jmax (resultsViewport.getHeight(), y));
}

void PreferencesPanel::revealEntry (const prefs::Entry& entry)
{
    // Clearing the box first, so the page comes back and the row can be marked
    // on it. A result that opened a page behind a list would be a result you
    // could not see the effect of.
    setFilter ({});
    selectPage (entry.page);

    for (const auto& page : pages)
        page.second->reveal (page.first == entry.page ? &entry : nullptr);
}

} // namespace dew
