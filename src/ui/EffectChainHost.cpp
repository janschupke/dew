#include "ui/EffectChainHost.h"

#include <utility>

#include "ui/design/Icons.h"
#include "ui/design/Tokens.h"

namespace dew
{

using namespace tokens;

EffectChainHost::EffectChainHost (ProjectDocument& d, EditorState& s, Orientation orientation)
    : chain (d, s)
{
    setComponentID ("effectChainHost");

    chain.setOrientation (orientation);

    // A ROW scrolls, a COLUMN does not.
    //
    // The mixer's chain is wider than the band it sits in, so it needs a
    // scroller of its own. The instrument panel's is already inside one -
    // MainComponent scrolls the whole sidebar - and a second viewport nested in
    // the first is what stopped an opened card ever being reachable: the inner
    // scroller absorbed the growth, so the panel never got taller and the
    // sidebar's own bar never came up. One scroller per axis, owned by whoever
    // is outermost.
    if (chain.isHorizontal())
    {
        viewport.setViewedComponent (&chain, false);
        viewport.setScrollBarsShown (false, true);
        addAndMakeVisible (viewport);
    }
    else
    {
        addAndMakeVisible (chain);
    }

    addButton.onClick = [this] { chain.showAddMenu (addButton); };
    addAndMakeVisible (addButton);

    chain.onRequiredSizeChanged = [this]
    {
        addButton.setEnabled (chain.canAddEffect());
        layOutChain();
        notifyPreferredHeightChanged();
    };

    // A card cannot be dragged past the edge of what is on screen without this.
    // The chain is the SCROLLED component, so a point in its coordinates is a
    // point in content space and has to be brought back into the scroller's -
    // which in a column is the sidebar's viewport, not one of ours.
    chain.onDragNearEdge = [this] (juce::Point<int> positionInChain)
    {
        auto* scroller = chain.isHorizontal() ? &viewport
                                              : findParentComponentOfClass<juce::Viewport>();

        if (scroller == nullptr)
            return;

        // getLocalPoint rather than two screen positions: a UI test paints into
        // an Image with no peer, where a screen position is not a screen's.
        const auto inScroller = scroller->getLocalPoint (&chain, positionInChain);

        scroller->autoScroll (inScroller.x, inScroller.y, autoScrollMargin, autoScrollSpeed);
    };

    addButton.setEnabled (chain.canAddEffect());
}

void EffectChainHost::setPresetHoverSink (std::function<void (const juce::String&)> sink)
{
    chain.onPresetHover = std::move (sink);
}

void EffectChainHost::setOwner (juce::ValueTree owner, juce::String name)
{
    chain.setOwner (std::move (owner));

    if (ownerName != name)
    {
        ownerName = std::move (name);
        repaint();
    }

    addButton.setEnabled (chain.canAddEffect());
}

int EffectChainHost::getPreferredHeight() const
{
    // The card's bottom inset is part of the height it asks for: leaving it out
    // is what clips the last few pixels of every card in the row.
    return tokens::size::stripHeading + chain.getRequiredHeight() + space::xs
           + (chain.isHorizontal() ? viewport.getScrollBarThickness() : 0);
}

void EffectChainHost::paint (juce::Graphics& g)
{
    // A BAND, not a card. The chain used to draw a rounded outlined container
    // around cards that already draw a rounded outlined body of their own -
    // two levels of containment saying the same thing, with a gap of window
    // background around the outer one so the row floated free of the strips it
    // belongs to. A full-bleed surface with a rule along its top says the same
    // thing once, and says it as a region rather than as an object.
    g.fillAll (colour::surface);

    g.setColour (colour::dividerStrong);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());

    paint::sectionHeading (
        g, { space::md, 0, getWidth() - size::iconButton - space::md, tokens::size::stripHeading },
        ownerName.isEmpty() ? "EFFECTS" : "EFFECTS - " + ownerName);

    // Between the heading and the cards, the way every other heading in the
    // app is separated from what it names.
    g.setColour (colour::divider);
    g.drawHorizontalLine (tokens::size::stripHeading - 1, (float) space::md,
                          (float) (getWidth() - space::md));
}

void EffectChainHost::resized()
{
    auto area = getLocalBounds();

    // The add button ends where the heading text begins, on the same margin the
    // band's own contents use - so it lines up with the panel's preset button
    // above it rather than sitting two pixels further out.
    auto heading = area.removeFromTop (tokens::size::stripHeading).reduced (space::md, space::xxs);

    addButton.setBounds (heading.removeFromRight (size::iconButton));

    // A column's cards line up with everything else in the panel, so the band's
    // margin is the panel's. A ROW's is smaller by the gap layOutCards already
    // leaves before the first card, which together come to the same thing.
    content = area.reduced (chain.isHorizontal() ? space::xs : space::md, 0)
                  .withTrimmedBottom (space::xs);

    if (chain.isHorizontal())
        viewport.setBounds (content);

    layOutChain();
}

void EffectChainHost::layOutChain()
{
    if (chain.isHorizontal())
    {
        // The scrollbar's height is reserved whether or not it is showing.
        // Asking the viewport what is currently visible depends on whether the
        // bar is up, which depends on the width being set right here - so the
        // two chase each other a frame at a time. Without the reservation the
        // bottom of every card is quietly clipped, since nothing scrolls
        // vertically to reveal it.
        chain.setSize (juce::jmax (viewport.getMaximumVisibleWidth(), chain.getRequiredWidth()),
                       juce::jmax (1, viewport.getHeight() - viewport.getScrollBarThickness()));
        return;
    }

    // A column takes exactly the height it asked for. Whoever stacks this asked
    // for getPreferredHeight() and is scrolling us; giving the chain "whatever
    // is left" here is what used to clip it instead.
    chain.setBounds (content.withHeight (chain.getRequiredHeight()));
}

void EffectChainHost::notifyPreferredHeightChanged()
{
    const auto wanted = getPreferredHeight();

    if (std::exchange (lastPreferredHeight, wanted) == wanted)
        return;

    if (onPreferredHeightChanged != nullptr)
        onPreferredHeightChanged();
}

} // namespace dew
