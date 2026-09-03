#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

#include "lang/Completion.h"

namespace dew
{

/** The completion popup.

    A plain child Component wrapping a ListBox, deliberately NOT a
    juce::PopupMenu. A PopupMenu is modal, which headless tests fight, and its
    MenuItemIterator holds a reference to a menu that may already be gone. This
    is a real component with a component ID: it can be found, painted offscreen,
    and driven with real KeyPresses, which is what makes any of it testable.

    It draws and it selects. It does not decide what goes in it and it does not
    insert anything - the editor owns both, because both are about the document.
*/
class ScoreCompletionList : public juce::Component, private juce::ListBoxModel
{
public:
    ScoreCompletionList();

    void setItems (std::vector<lang::Completion>);

    const std::vector<lang::Completion>& getItems() const
    {
        return items;
    }

    /** The candidate that Return would insert, or nullptr when empty. */
    const lang::Completion* getSelected() const;

    void moveSelection (int delta);

    /** How tall this wants to be for the items it holds, capped so a long list
        does not cover the line it is completing.
    */
    int preferredHeight() const;

    void resized() override;
    void paint (juce::Graphics&) override;

    /** Fired when a row is clicked or Return is pressed on it. */
    std::function<void()> onAccept;

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;

    std::vector<lang::Completion> items;
    juce::ListBox list { "scoreCompletionRows", this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScoreCompletionList)
};

} // namespace dew
