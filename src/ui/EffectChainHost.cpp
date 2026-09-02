#include "ui/EffectChainHost.h"

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

    // A chain scrolls along its own axis and only that one: the other dimension
    // is whatever the host gives it, so a second bar would never have anything
    // to scroll.
    viewport.setViewedComponent (&chain, false);
    viewport.setScrollBarsShown (! chain.isHorizontal(), chain.isHorizontal());
    addAndMakeVisible (viewport);

    addButton.onClick = [this] { chain.showAddMenu (addButton); };
    addAndMakeVisible (addButton);

    chain.onRequiredSizeChanged = [this]
    {
        addButton.setEnabled (chain.canAddEffect());
        layOutChain();
    };

    addButton.setEnabled (chain.canAddEffect());
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
    return headingHeight + chain.getRequiredHeight() + space::xs
           + (chain.isHorizontal() ? viewport.getScrollBarThickness() : 0);
}

void EffectChainHost::paint (juce::Graphics& g)
{
    // A card under the whole chain, heading included. Without it the chain sat
    // straight on the window background with nothing to say where it began, so
    // in the mixer it read as loose controls under the strips rather than as a
    // panel belonging to the selected one.
    paint::container (g, getLocalBounds());

    paint::sectionHeading (g, { space::md, 0, getWidth() - size::iconButton - space::md,
                                headingHeight },
                           ownerName.isEmpty() ? "EFFECTS" : "EFFECTS - " + ownerName);

    // Between the heading and the cards, the way every other heading in the
    // app is separated from what it names.
    g.setColour (colour::divider);
    g.drawHorizontalLine (headingHeight - 1, (float) space::md,
                          (float) (getWidth() - space::md));
}

void EffectChainHost::resized()
{
    auto area = getLocalBounds().reduced (space::xs, 0);
    auto heading = area.removeFromTop (headingHeight);

    addButton.setBounds (heading.removeFromRight (size::iconButton).reduced (space::xxs));

    viewport.setBounds (area.withTrimmedBottom (space::xs));
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

    chain.setSize (juce::jmax (120, viewport.getMaximumVisibleWidth()),
                   juce::jmax (viewport.getMaximumVisibleHeight(), chain.getRequiredHeight()));
}

} // namespace dew
