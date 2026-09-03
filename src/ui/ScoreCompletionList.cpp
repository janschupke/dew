#include "ui/ScoreCompletionList.h"

#include "ui/design/Tokens.h"

namespace dew
{

namespace
{

/** How many rows are visible before the list scrolls. Enough to see the shape
    of what is on offer, few enough that the popup does not cover the code it is
    completing.
*/
constexpr int visibleRows = 8;

juce::Colour colourFor (lang::CompletionKind kind)
{
    switch (kind)
    {
        case lang::CompletionKind::block: return tokens::colour::accent;
        case lang::CompletionKind::key: return tokens::colour::accent;
        case lang::CompletionKind::name: return tokens::colour::success;
        case lang::CompletionKind::value: break;
    }

    return tokens::colour::textPrimary;
}

} // namespace

ScoreCompletionList::ScoreCompletionList()
{
    setComponentID ("scoreCompletion");

    list.setRowHeight (tokens::size::controlHeightSm);
    list.setColour (juce::ListBox::backgroundColourId, tokens::colour::surface);
    list.setOutlineThickness (0);
    addAndMakeVisible (list);
}

void ScoreCompletionList::setItems (std::vector<lang::Completion> newItems)
{
    items = std::move (newItems);
    list.updateContent();
    list.selectRow (items.empty() ? -1 : 0);
    list.repaint();
}

const lang::Completion* ScoreCompletionList::getSelected() const
{
    const auto row = list.getSelectedRow();

    if (row < 0 || row >= (int) items.size())
        return nullptr;

    return &items[(std::size_t) row];
}

void ScoreCompletionList::moveSelection (int delta)
{
    if (items.empty())
        return;

    const auto count = (int) items.size();
    const auto from = juce::jmax (0, list.getSelectedRow());

    // Wraps, because a list you can walk off the end of makes you look at where
    // the selection went instead of at the code.
    const auto to = ((from + delta) % count + count) % count;

    list.selectRow (to);
}

int ScoreCompletionList::preferredHeight() const
{
    const auto rows = juce::jlimit (1, visibleRows, (int) items.size());
    return rows * tokens::size::controlHeightSm + tokens::space::xs;
}

void ScoreCompletionList::resized()
{
    list.setBounds (getLocalBounds().reduced (tokens::space::xxs));
}

void ScoreCompletionList::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.setColour (tokens::colour::surface);
    g.fillRoundedRectangle (bounds, tokens::radius::sm);

    g.setColour (tokens::colour::outline);
    g.drawRoundedRectangle (bounds.reduced (tokens::stroke::whisper), tokens::radius::sm,
                            tokens::stroke::hairline);
}

int ScoreCompletionList::getNumRows()
{
    return (int) items.size();
}

void ScoreCompletionList::paintListBoxItem (int row, juce::Graphics& g, int width, int height,
                                            bool selected)
{
    if (row < 0 || row >= (int) items.size())
        return;

    const auto& item = items[(std::size_t) row];

    if (selected)
        g.fillAll (tokens::colour::accent.withAlpha (tokens::emphasis::wash));

    auto area = juce::Rectangle<int> (0, 0, width, height).reduced (tokens::space::sm, 0);

    g.setFont (tokens::type::monospaced (tokens::type::small));
    g.setColour (colourFor (item.kind));

    const auto textWidth = juce::jmin (
        area.getWidth() / 2,
        juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), item.text)
            + tokens::space::md);

    g.drawText (item.text, area.removeFromLeft (textWidth), juce::Justification::centredLeft);

    // The schema's own doc string, which is what makes this a reference rather
    // than a list of words.
    g.setFont (tokens::type::font (tokens::type::caption));
    g.setColour (tokens::colour::textSecondary);
    g.drawText (item.detail, area, juce::Justification::centredLeft, true);
}

void ScoreCompletionList::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    list.selectRow (row);
}

void ScoreCompletionList::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    list.selectRow (row);

    if (onAccept != nullptr)
        onAccept();
}

} // namespace dew
