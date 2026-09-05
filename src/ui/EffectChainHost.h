#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "i18n/Strings.h"

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

    EffectChainComponent& getChain() noexcept
    {
        return chain;
    }

    /** Where a preset row's description goes as the pointer passes over it.

        The status strip answers at once where the floating tooltip waits, which
        is the pair HoverHelp gives every other control - and a popup menu is a
        window of its own, so HoverHelp's own listener cannot reach these rows.
    */
    void setPresetHoverSink (std::function<void (const juce::String&)>);

    /** Heading, chain and - in a row - the scrollbar under it. Whoever stacks
        this asks rather than repeating the arithmetic.
    */
    int getPreferredHeight() const;

    /** How many rows of knobs the cards in this band lay out on.

        A ROW only. A column folds a card away instead, and is scrolled by
        whoever holds it, so there is no band height there to spend.
    */
    void setKnobRows (int rows);
    int getKnobRows() const noexcept
    {
        return chain.getKnobRowBudget();
    }

    /** The most rows whose band would still fit in `height`, never fewer than
        one. Whoever owns the height asks this rather than inverting
        getPreferredHeight by hand - the band is a whole number of knob rows and
        a height between two of them is ground no knob can use.
    */
    int knobRowsFitting (int height) const;

    /** The band's top edge, which is also the rule it paints there.

        Latched and dragged, and a report rather than an edit at both: the
        height belongs to whoever stacks this band, the same division
        PlaylistTrackHeader keeps with the playlist that owns its lane height.
        The delta is in SCREEN pixels and measured from the press, because this
        band's own frame moves under the pointer while the drag reads it.

        There is no release hook, unlike the playlist's. Its lane height is a
        pixel and its scrollbar has to be told; a band is a whole number of knob
        rows, so letting go changes nothing that was not already true.
    */
    std::function<void()> onResizeBegin;
    std::function<void (int deltaY)> onResizeDrag;

    /** Whether a point is on the grab band. Public because a test aims at it
        rather than guessing four pixels. */
    bool isOnResizeEdge (juce::Point<int> position) const;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    /** Fired when getPreferredHeight() moves - a card opened or closed, or a
        chain rebuilt.

        A column has no scroller of its own, so the host it is stacked in has to
        lay out again or the growth goes nowhere. That is exactly what used to
        happen: the inner viewport swallowed it, the sidebar never got taller,
        and an opened card at the bottom of a full chain was unreachable.
    */
    std::function<void()> onPreferredHeightChanged;

private:
    /** How near the edge a dragged card has to get before the view follows it,
        and how fast it then moves. */
    static constexpr int autoScrollMargin = tokens::space::xxl;
    static constexpr int autoScrollSpeed = tokens::space::lg;

    /** How deep the grab band along the top edge is.

        A row of its own would be a control; this is an edge, and an edge has to
        be thin enough that the heading behind it is still a heading. The same
        four pixels a playlist header gives its own, and the pointer shape is
        what makes it findable.
    */
    static constexpr int resizeBandHeight = tokens::space::xs;

    bool isResizable() const noexcept;

    /** What a band this many knob rows deep costs, heading and scrollbar
        included. getPreferredHeight is this at the budget in force, and
        knobRowsFitting walks it - one formula rather than one and its inverse. */
    int bandHeightForRows (int rows) const;

    bool resizing = false;
    bool hoveringEdge = false;
    int resizeOriginY = 0;

    void layOutChain();
    void notifyPreferredHeightChanged();

    juce::String ownerName;

    /** Below the heading: what the chain is laid out in. */
    juce::Rectangle<int> content;

    /** What onPreferredHeightChanged last reported, so a rebuild that does not
        move the height does not make the whole window lay out again. */
    int lastPreferredHeight = 0;

    EffectChainComponent chain;

    /** A ROW's scroller. Unused, and not in the component tree at all, when the
        chain runs as a column - see the constructor. */
    juce::Viewport viewport;
    DewIconButton addButton { icons::plus(), tr (StringId::effect_add_help) };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectChainHost)
};

} // namespace dew
