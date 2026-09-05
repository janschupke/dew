#include "ui/EffectChainHost.h"

#include <utility>

#include "ui/design/Cursors.h"
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

int EffectChainHost::bandHeightForRows (int rows) const
{
    return tokens::size::stripHeading + EffectChainComponent::requiredHeightForRows (rows)
           + space::xs + viewport.getScrollBarThickness();
}

int EffectChainHost::getPreferredHeight() const
{
    // The card's bottom inset is part of the height it asks for: leaving it out
    // is what clips the last few pixels of every card in the row.
    if (chain.isHorizontal())
        return bandHeightForRows (chain.getKnobRowBudget());

    return tokens::size::stripHeading + chain.getRequiredHeight() + space::xs;
}

void EffectChainHost::setKnobRows (int rows)
{
    if (! isResizable())
        return;

    chain.setKnobRowBudget (rows);
}

int EffectChainHost::knobRowsFitting (int height) const
{
    // Walked down from the top rather than solved for, because bandHeightForRows
    // is the only thing that knows what a band costs besides its knobs - the
    // heading, the scrollbar reserved whether or not it is showing, and the
    // insets - and a closed form here would be a second copy of it. Four rungs
    // is not a search worth being clever about.
    auto answer = tokens::size::effectBandRowsMin;

    for (auto rows = tokens::size::effectBandRowsMax; rows > answer; --rows)
    {
        if (bandHeightForRows (rows) <= height)
        {
            answer = rows;
            break;
        }
    }

    return answer;
}

bool EffectChainHost::isResizable() const noexcept
{
    return chain.isHorizontal();
}

bool EffectChainHost::isOnResizeEdge (juce::Point<int> position) const
{
    return isResizable() && position.y < resizeBandHeight;
}

void EffectChainHost::mouseMove (const juce::MouseEvent& event)
{
    const auto onEdge = isOnResizeEdge (event.getPosition());

    setMouseCursor (onEdge ? cursor::value : cursor::idle);

    if (std::exchange (hoveringEdge, onEdge) != onEdge)
        repaint();
}

void EffectChainHost::mouseDown (const juce::MouseEvent& event)
{
    // The right button moves nothing. A latch rather than a second read of the
    // modifiers on the drag, which is the rule every other gesture in dew that
    // is not a Dew primitive keeps.
    if (event.mods.isPopupMenu() || ! isOnResizeEdge (event.getPosition()))
        return;

    resizing = true;
    resizeOriginY = event.getScreenPosition().y;

    if (onResizeBegin != nullptr)
        onResizeBegin();

    repaint();
}

void EffectChainHost::mouseDrag (const juce::MouseEvent& event)
{
    // SCREEN coordinates, and a delta from the PRESS rather than accumulated
    // between samples. This band's own top edge is what the drag is moving, so
    // its local frame travels under the pointer while the pointer is being read
    // in it - and a delta summed sample by sample is path-dependent, which is
    // the defect the effect chain's own reorder is frozen against.
    if (resizing && onResizeDrag != nullptr)
        onResizeDrag (event.getScreenPosition().y - resizeOriginY);
}

void EffectChainHost::mouseUp (const juce::MouseEvent&)
{
    if (std::exchange (resizing, false))
        repaint();
}

void EffectChainHost::mouseExit (const juce::MouseEvent&)
{
    if (std::exchange (hoveringEdge, false))
        repaint();
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

    // The band's top rule is also its grab band, so it answers the pointer the
    // way the panel divider does - a rule you can drag has to say so before you
    // press it, and this one had looked exactly like the rule below the strips.
    g.setColour (resizing || hoveringEdge ? colour::accent : colour::dividerStrong);
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

    // And a card down a column reflows on the width it is given, so the height
    // asked for BEFORE that width was set can be the answer for the panel's
    // last one. Telling whoever stacks us settles it in one more pass and no
    // more: notifyPreferredHeightChanged records the height before it calls
    // out, so the layout it provokes finds nothing left to report.
    notifyPreferredHeightChanged();
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
