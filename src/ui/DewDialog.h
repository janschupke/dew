#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

namespace dew::dialog
{

/** Opens a panel as a dialog, with dew's window conventions applied once.

    Six call sites set the same six fields - background, centring, escape-to-
    close, native title bar, not resizable - and differed only in what they were
    showing and what it was called. The conventions are the point: a dialog that
    resizes, or that ignores escape, is a dialog that behaves unlike the other
    five.

    Takes ownership of `content`, as LaunchOptions::content.setOwned does. The
    dialog is modeless and deletes itself when closed.
*/
void launch (juce::Component* content, const juce::String& title, juce::Component* centreAround);

/** The tallest a dialog's content may be and still fit on this screen.

    dew's dialogs are not resizable, by convention, and the render panel grows
    itself as rows appear. Neither fact is a problem until the interface is
    scaled: at 1.75x a 470-tall panel wants 822 logical pixels of a screen that
    has fewer, and because its buttons are laid out from the BOTTOM they are the
    part that goes off the edge - on a dialog that cannot be resized or moved
    far enough to bring them back.

    So a dialog taller than this scrolls instead, and anything that resizes
    itself clamps to it.
*/
int maxContentHeight();

// -----------------------------------------------------------------------------

/** What a dew dialog's content IS.

    Nine panels were each a plain juce::Component that restated the same four
    things by hand: the inset they lay out inside, the ground they fill, how to
    close the window they are in, and the footer their buttons sit on. The
    copies had drifted - seven button widths across five files, and one label
    column at 74 where every other panel's is 76 - because nothing related them.

    A panel can also be EMBEDDED, which is what the preferences window does with
    three of them. An embedded panel insets nothing and paints no ground,
    because the pane around it already did both: a panel that insets itself
    inside a pane that has already been inset puts its controls 32 pixels from
    an edge every other panel sits 16 from.

    It does NOT own the preferred size. That stays a `static constexpr int` on
    each panel, because dew_shot templates over it (tools/ShotPanels.h) and
    ReflowTests asserts against it - neither of which has an instance to ask.
*/
class Panel : public juce::Component
{
public:
    /** Stated rather than implicit: JUCE_DECLARE_NON_COPYABLE below declares a
        copy constructor, which is enough to suppress the one the compiler would
        otherwise write - and every panel deriving from this has its own. */
    Panel() = default;

    /** The rectangle to lay out and to paint inside.

        Every panel opened `getLocalBounds().reduced (space::xl)` at the top of
        both resized() and paint(), and several opened it a second time further
        down to reach a strip off the bottom. This is that rectangle, and it is
        the only thing that knows about the inset.
    */
    juce::Rectangle<int> contentBounds() const;

    /** How much taller a panel is than its content: the inset, twice, or zero
        when embedded. RenderPanel adds this to the rows it is showing. */
    int chromeHeight() const;

    /** Laid out inside another window's pane rather than in a dialog of its own. */
    void setEmbedded (bool);

    bool isEmbedded() const noexcept
    {
        return embedded;
    }

    /** Right-aligned buttons off the bottom of `area`, acting button outermost,
        space::md apart, each as wide as its own words need.

        @returns the strip they were placed on, so a caller that paints into the
                 footer has the same rectangle the layout used.
    */
    juce::Rectangle<int> layOutFooter (juce::Rectangle<int>& area,
                                       std::initializer_list<juce::Button*> rightToLeft);

    /** Closes the window this panel is in.

        Null when the panel was built bare, which is how dew_shot and the tests
        drive it: there is no dialog to leave, and the buttons still do their
        work. Four panels each had a private copy of this.
    */
    void close();

protected:
    /** The ground - unless something else has already painted it. */
    void paintBackground (juce::Graphics&) const;

private:
    bool embedded = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Panel)
};

/** How wide a footer button has to be to hold its own label.

    Measured rather than chosen. Five panels stated a width by hand, and the
    comment beside one of them says why that is not good enough: at 140 the
    primary button read "Allow reading and changi", which is invisible in the
    code and obvious in one render. A translated label is longer than the
    English it was measured against, and nobody re-measures.
*/
int buttonWidthFor (const juce::Button&);

} // namespace dew::dialog
