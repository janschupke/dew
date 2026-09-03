#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/ProjectDocument.h"
#include "ui/EditorState.h"
#include "ui/EffectChainComponent.h"
#include "ui/design/Tokens.h"
#include "ui/primitives/DewControls.h"

namespace dew
{

/** An effect chain with its heading, its add button and its scrolling.

    The mixer and the instrument panel both show a chain, and both used to wire
    up their own Viewport and size its content with the same four lines. Two
    identical copies are tolerable while they stay identical - but the mixer's
    chain now runs the other way round, so they were about to become two copies
    that differ, which is how two things that should agree stop agreeing.

    The heading and the add button belong here rather than to the chain because
    the chain is the SCROLLED component. Laid out as a row its width is the
    content width, not the visible width, so a button placed against its right
    edge would sit off the end of a row you have to scroll to reach.
*/
class EffectChainHost : public juce::Component
{
public:
    using Orientation = EffectChainComponent::Orientation;

    EffectChainHost (ProjectDocument&, EditorState&, Orientation);

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Points the chain at a channel or mixer track. The name is the heading's:
        "EFFECTS - Insert 1". An invalid tree shows the empty state.
    */
    void setOwner (juce::ValueTree owner, juce::String name);

    EffectChainComponent& getChain() noexcept { return chain; }

    /** Heading, chain and - in a row - the scrollbar under it. Whoever stacks
        this asks rather than repeating the arithmetic.
    */
    int getPreferredHeight() const;


private:
    /** How near the edge a dragged card has to get before the view follows it,
        and how fast it then moves. */
    static constexpr int autoScrollMargin = tokens::space::xxl;
    static constexpr int autoScrollSpeed = tokens::space::lg;

    void layOutChain();

    juce::String ownerName;

    EffectChainComponent chain;
    juce::Viewport viewport;
    DewIconButton addButton { icons::plus(), "Add an effect" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectChainHost)
};

} // namespace dew
