#pragma once

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/ProjectDocument.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "ui/EditorState.h"
#include "ui/EffectChainComponent.h"
#include "ui/design/Animator.h"
#include "ui/design/Gestures.h"

/** A chain editor laid out and visible, so a card can be dragged at it.

    Shared by the chain's UI tests and its reorder-drag tests. layOutLikeAHost
    is here rather than in either, because a bare chain has no EffectChainHost
    to give it the size it asked for and a test that forgets is testing a
    zero-height card.
*/
namespace dew::testing
{

struct ChainHarness
{
    explicit ChainHarness (
        EffectChainComponent::Orientation orientation = EffectChainComponent::Orientation::vertical)
    {
        document.setState (ProjectFactory::createDefault(), true);
        chain.setOrientation (orientation);
        chain.setSize (300, 400);
        chain.setVisible (true);
        chain.setOwner (channel());
    }

    /** What EffectChainHost does after a rebuild: give the chain the size it
        asked for along the axis it runs. A bare chain has no host to do it.
    */
    void layOutLikeAHost()
    {
        if (chain.isHorizontal())
            chain.setSize (juce::jmax (300, chain.getRequiredWidth()), 140);
        else
            chain.setSize (300, juce::jmax (400, chain.getRequiredHeight()));
    }

    /** The cards, by SLOT. Not by child order: a card being dragged is brought
        to the front, so the chain's children are in paint order during a drag.
    */
    juce::Array<juce::Rectangle<int>> cardBounds()
    {
        juce::Array<juce::Rectangle<int>> bounds;

        for (int i = 0; i < chain.getNumSlotRows(); ++i)
            bounds.add (chain.getSlotBounds (i));

        return bounds;
    }

    juce::Point<int> centreOf (int slot)
    {
        return chain.getSlotBounds (slot).getCentre();
    }

    /** Walks the pointer from a card to a point in the chain's coordinates.

        Driven through the chain's own seam rather than through synthetic mouse
        events: MouseEvent::mouseWasDraggedSinceMouseDown asks the mouse SOURCE,
        which no synthetic event ever pressed, so a drag assembled out of
        MouseEvents proves nothing here.
    */
    void dragFrom (int slot, juce::Point<int> to, int steps = 8)
    {
        const auto start = centreOf (slot);

        chain.beginReorder (slot, start);

        for (int i = 1; i <= steps; ++i)
            chain.updateReorder (
                { start.x + (to.x - start.x) * i / steps, start.y + (to.y - start.y) * i / steps });
    }

    /** Where a drop at `to` would put the card, without letting go. */
    int insertionAfterDragging (int slot, juce::Point<int> to)
    {
        dragFrom (slot, to);
        const auto insertion = chain.getReorderInsertion();
        chain.endReorder (false);

        return insertion;
    }

    /** Far enough along the chain to be past every card. */
    juce::Point<int> pastTheEnd() const
    {
        return chain.isHorizontal() ? juce::Point<int> { chain.getWidth(), 0 }
                                    : juce::Point<int> { 0, chain.getHeight() };
    }

    juce::Point<int> beforeTheStart() const
    {
        return { 0, 0 };
    }

    juce::ValueTree channel()
    {
        return document.getState().getChildWithName (ids::CHANNEL);
    }

    juce::StringArray typesInOrder()
    {
        juce::StringArray types;

        for (const auto& child : channel())
            if (child.hasType (ids::EFFECT))
                types.add (child[ids::type].toString());

        return types;
    }

    ProjectDocument document;
    EditorState editorState;
    EffectChainComponent chain { document, editorState };
};

} // namespace dew::testing
