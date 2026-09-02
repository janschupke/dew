#include "EffectChainHost.h"

#include "design/Icons.h"
#include "design/Tokens.h"

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
    return headingHeight + chain.getRequiredHeight()
           + (chain.isHorizontal() ? viewport.getScrollBarThickness() : 0);
}

void EffectChainHost::paint (juce::Graphics& g)
{
    paint::sectionHeading (g, { space::xxs, 0, getWidth() - size::iconButton, headingHeight },
                           ownerName.isEmpty() ? "EFFECTS" : "EFFECTS - " + ownerName);
}

void EffectChainHost::resized()
{
    auto area = getLocalBounds();
    auto heading = area.removeFromTop (headingHeight);

    addButton.setBounds (heading.removeFromRight (size::iconButton).reduced (space::xxs));

    viewport.setBounds (area);
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
