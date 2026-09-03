#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/ProjectDocument.h"
#include "ui/design/Animator.h"
#include "ui/EditorState.h"
#include "ui/primitives/DewControls.h"
#include "ui/primitives/DewNumberField.h"

namespace dew
{

/** The effect chain of one channel or mixer track.

    Deliberately owner-agnostic: a channel and a mixer track carry the same
    EFFECT children, so the same editor drives both rather than two that can
    drift apart. Point it at a node with setOwner().

    It lays out either way round, because the two places a chain appears have
    opposite shapes. The instrument panel is a narrow column, so its chain is a
    vertical accordion: each effect is a card whose header is always visible -
    grip, bypass, icon, name, expand chevron, reorder, remove - and clicking the
    header opens the card in place. Several can be open at once, which is the
    point: comparing a filter's cutoff against a delay's time used to mean
    clicking between them.

    The mixer is a wide band under the strips, so its chain is a single
    horizontal row of cards, all open, scrolling sideways. Folding a card there
    would only make the row ragged, so in horizontal the expand chevron is
    hidden and every card is the same height.

    Which cards are open is view state, kept in EditorState against the effect's
    id, so it survives a rebuild, is not on the undo stack, and does not make the
    project dirty.

    It does NOT draw its own heading. In horizontal its width is the scrolling
    content width, which can be far wider than the viewport, so a heading and an
    add button laid out against it would sit off the right-hand edge. Both belong
    to EffectChainHost, outside the Viewport.
*/
class EffectChainComponent : public juce::Component,
                             private juce::ValueTree::Listener,
                             private juce::ChangeListener
{
public:
    /** Which way the cards run. Vertical is the default: it is the shape the
        chain had before the mixer needed the other one, and the instrument
        panel still depends on every part of it.
    */
    enum class Orientation { vertical, horizontal };

    EffectChainComponent (ProjectDocument&, EditorState&);
    ~EffectChainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Escape abandons a reorder. Read through the hotkey registry rather than
        spelled here, so there is still exactly one place a key is named. */
    bool keyPressed (const juce::KeyPress&) override;

    void setOrientation (Orientation);
    bool isHorizontal() const noexcept { return orientation == Orientation::horizontal; }

    /** Points the editor at a channel or mixer track. An invalid tree shows the
        empty state rather than the previous owner's chain.
    */
    void setOwner (juce::ValueTree owner);
    const juce::ValueTree& getOwner() const noexcept { return chainOwner; }

    /** Whether another effect would fit. The add button lives on the host, so
        the host asks rather than the chain reaching out to enable it.
    */
    bool canAddEffect() const;

    /** Offers the effect types. Public because the add button is the host's. */
    void showAddMenu (juce::Component& target);

    /** The size the whole chain needs along its own axis. Its host puts it in a
        Viewport, so a full chain scrolls rather than being silently clipped.

        Horizontally the height is a constant - one card - so the mixer's effect
        row does not change height between the empty and the populated state.
    */
    int getRequiredHeight() const;
    int getRequiredWidth() const;

    /** The slot a point in the chain's own coordinates falls in, clamped to the
        chain. Used to reorder by dragging a grip, which is why it is a hit test
        rather than a distance divided by a row height: cards are neither all the
        same height when they fold nor all the same width when they are a row.
    */
    int slotAtPosition (juce::Point<int> position) const;

    // --- for tests -----------------------------------------------------------
    int getNumSlotRows() const { return cards.size(); }
    int getSelectedSlot() const noexcept { return selectedSlot; }
    void selectSlot (int index);
    void addEffectOfType (const juce::String& type);

    /** Opens or closes one card, by position in the chain. */
    void setSlotExpanded (int index, bool expanded);
    bool isSlotExpanded (int index) const;

    /** Moves a card by dragging its grip, in the same terms the grip uses. */
    void moveSlot (int from, int to);

    // --- reordering ----------------------------------------------------------
    /** Starts a reorder from a card's grip, in the chain's own coordinates.

        The gesture's state lives HERE rather than on the card, and that is the
        whole point of it. Committing a move rebuilds the chain, so a card that
        owned its own drag was deleted in the middle of its own mouseDrag - and
        the line after the commit then wrote a flag into freed memory.

        Nothing happens until the pointer has travelled gesture::dragThresholdPx.
        Before this, a drag committed the moment the cursor crossed into a
        neighbouring card and then cancelled itself, so a card could only ever
        move one slot per press and a twitch on the grip reordered the chain.
    */
    void beginReorder (int slot, juce::Point<int> positionInChain);
    void updateReorder (juce::Point<int> positionInChain);

    /** Ends the gesture. `false` cancels it: the cards go back to where they
        were and nothing reaches the document at all.

        A commit is ONE undo transaction for the whole drag, however many cards
        the pointer crossed on the way.
    */
    void endReorder (bool commit);

    bool isReordering() const noexcept { return reorder.active; }

    /** Where the dragged card would land, or -1 when nothing is being dragged.
        A FINAL index - what moveSlot would be given. */
    int getReorderInsertion() const noexcept { return reorder.active ? reorder.insertAt : -1; }

    /** The gap the other cards have opened for it, in the chain's coordinates.
        Empty unless a drag is in progress. */
    juce::Rectangle<int> getDropArea() const noexcept { return dropArea; }

    /** Where one card is, by its position in the chain.

        By SLOT, deliberately: a card being dragged is brought to the front, so
        the chain's child order is a paint order during a drag and not the
        order of the chain.
    */
    juce::Rectangle<int> getSlotBounds (int index) const;

private:
    class Card;

    /** A reorder in progress.

        `frozen` is the load-bearing part. Every hit test runs against where the
        cards were when the drag STARTED, never against where they are now: the
        gap this drag opens moves every card after it, so a hit test reading
        live bounds would answer a question the drag itself had just changed,
        and the insertion point would flip back and forth across a boundary the
        pointer was not crossing.
    */
    struct Reorder
    {
        bool active = false;         ///< past the threshold
        int source = -1;             ///< the slot pressed, or -1 for no gesture
        int insertAt = -1;           ///< the final index it would land at
        juce::Point<int> pressedAt, cursor, grabOffset;
        juce::Array<juce::Rectangle<int>> frozen;
    };

    /** Where the pointer would drop the card, as a final index. Monotonic in
        the pointer's position along the chain, which is what makes it
        independent of the path taken to get there. */
    int insertionFor (juce::Point<int>) const;

    /** Works out where every card belongs and aims it there. */
    void layOutCards();

    /** Moves the cards to wherever their eased positions have got to. Called
        from the animator as well as from layOutCards, so it recomputes
        nothing. */
    void applyCardPositions();

    Reorder reorder;
    juce::Rectangle<int> dropArea;

    /** One per card, holding its position ALONG the chain. A card's size does
        not change during a drag, so its offset is the only thing that moves,
        and one eased scalar each is the whole animation.
    */
    juce::OwnedArray<ComponentMotion> slide;

    /** Whether the next layout arrives rather than sweeps. True after a
        rebuild, because a card being given its position for the first time has
        nowhere to have come from. */
    bool snapNextLayout = true;

    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void rebuild();
    void notifyRequiredSizeChanged();

    juce::ValueTree effectAt (int index) const;

    ProjectDocument& document;
    EditorState& editorState;
    juce::ValueTree chainOwner;

    Orientation orientation = Orientation::vertical;
    juce::OwnedArray<Card> cards;

    int selectedSlot = 0;
    bool rebuilding = false;

public:
    /** Called when the chain's required size changes, so a host that scrolls it
        can resize its content and refresh its add button. Set by whoever owns
        the Viewport.

        Deliberately not fired from resized(): the host answers this by calling
        setSize(), which calls resized(), and that would not terminate.
    */
    std::function<void()> onRequiredSizeChanged;

    /** Called while a card is being dragged, with the pointer in the chain's
        own coordinates, so a host that SCROLLS the chain can follow it.

        The mixer's chain is a row wider than its viewport, and without this a
        card cannot be dragged past the edge of what is on screen. Set by
        whoever owns the Viewport, exactly like onRequiredSizeChanged: the
        chain is the scrolled component and cannot reach the thing scrolling it.
    */
    std::function<void (juce::Point<int>)> onDragNearEdge;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectChainComponent)
};

} // namespace dew
