#pragma once

#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/EffectType.h"
#include "model/ParamSpec.h"

#include "app/ProjectDocument.h"
#include "ui/EditorState.h"
#include "ui/primitives/DewControls.h"
#include "ui/primitives/DewNumberField.h"
#include "ui/primitives/HoverTracker.h"
#include "ui/design/Tokens.h"

namespace dew
{

class EffectChainComponent;

/** One effect in a chain: a header that names it, and its parameters below or
    beside it.

    A nested class of EffectChainComponent until it was 537 lines of a 1202-line
    file. Unlike the rack's and the playlist's headers, this one is NOT
    independent of its host and is not pretended to be: it asks the chain which
    way it runs, tells it which slot was selected, and hands it the three phases
    of a reorder drag. Turning twenty such calls into twenty std::functions
    would be the "free function taking a dozen parameters" the paint split was
    written to avoid. It keeps a reference instead.

    What it owns is a card: the header's layout, the parameter grid, and the
    controls built from the catalog's ParamSpecs. The DRAG belongs to the chain,
    which is what lets a committed reorder delete this card while one of its own
    handlers is still on the stack.
*/
class EffectCard : public juce::Component
{
public:
    static constexpr int columns = 3; ///< down a column
    static constexpr int numberFieldHeight = 40;
    static constexpr int captionHeight = 12;

    // A column is sized rather than stretched: a number field wider than a hand
    // is not easier to drag, only emptier. 88 is what the gallery gives a knob
    // (72 wide) and a number field (86) with room for the cell inset.
    static constexpr int paramColumnWidth = 88;
    static constexpr int modeColumnWidth = 120; ///< fits "Low pass" and the chevron

    /** What the header packs: grip, bypass, icon, name, preset, reorder,
        remove and - vertically - the expand chevron. */
    static constexpr int cardMinWidth = 276 + tokens::size::minTouchTarget;

    /** Every card in a row is this tall. The chain quotes it to size the band. */
    static constexpr int cardHeight = tokens::size::rowHeight + tokens::size::knobRow
                                      + tokens::space::sm;

    EffectCard (EffectChainComponent& owner, ProjectDocument&, EditorState&, juce::ValueTree effect,
                int index);

    int getEffectId() const
    {
        return (int) effect[ids::id];
    }

    bool isExpanded() const;

    void setSelected (bool shouldBeSelected);

    /** Header plus, when open, the parameters.

        In a row every card is the same height whatever it holds. Cards of
        different heights side by side do not read as a row, and nothing folds
        there anyway.
    */
    int getRequiredHeight() const;

    /** How wide the card has to be for its header and its one row of controls.

        Only meaningful in a row; in a column a card is given the chain's width.
        The floor is what the header packs: the grip, bypass and type icon on
        the left, reorder and remove on the right, and enough between them for
        the effect's name beside a "BYPASSED" that is drawn into the same space.
    */
    int getRequiredWidth() const;

    void refreshValues();

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    /** Whether the parameters are showing. Folding is a column behaviour; in a
        row every card is open, so there is nothing for the chevron to do.
    */
    bool showsParameters() const;

    /** Three to a row down a column, everything on one row across a band.

        jmax because parametersFor() has a fallback that returns nothing, and a
        column count of zero is both an infinite loop and a divide by zero in
        layOutParams.
    */
    int columnCount() const;

    /** The header is identical whichever way the chain runs - only the reorder
        arrows change, because they point the way the chain goes.
    */
    void layOutHeader (juce::Rectangle<int> bounds);

    /** The parameter grid. Shared by both orientations: they differ only in how
        many columns there are and in the rectangle they hand it.
    */
    void layOutParams (juce::Rectangle<int> area, bool visible);

    struct ParamWidget
    {
        std::unique_ptr<DewKnob> knob;
        std::unique_ptr<DewNumberField> field;
        juce::Identifier property;
    };

    void write (const juce::Identifier& property, double value);
    void buildParameters();

    /** The dropdown for a type's one choice parameter, built from its spec. */
    void buildChoice (const ParamSpec& spec);

    /** The item id the stored value names, one-based the way a ComboBox is. */
    int selectedChoiceId() const;

    EffectChainComponent& owner;
    ProjectDocument& document;
    EditorState& editorState;
    juce::ValueTree effect;
    int index = 0;
    EffectType type = EffectType::filter;

    bool selected = false;
    HoverTracker hover { *this };
    juce::Point<int> pressedAt;
    bool updating = false;
    bool draggingFromGrip = false;

    /** The right button, refused for the whole press. A card is not a Dew
        primitive - it is a Component with its own gesture - so it carries the
        latch itself. See PopupPress. */
    PopupPress popupPress;
    bool inDrag = false;
    bool gestureActive = false;

    juce::Rectangle<int> gripBounds, iconBounds, nameBounds, modeCaptionBounds;

    DewIconButton bypassButton { icons::power(), "Bypass this effect" };
    DewIconButton expandButton { icons::chevronDown(), "Show or hide this effect's controls" };
    DewIconButton upButton { icons::chevronUp(), "Move earlier in the chain" };
    DewIconButton downButton { icons::chevronDown(), "Move later in the chain" };
    DewIconButton presetButton { icons::preset(), "Load a preset for this effect" };
    DewIconButton removeButton { icons::trash(), "Remove this effect",
                                 DewIconButton::Role::danger };

    juce::OwnedArray<ParamWidget> params;
    std::unique_ptr<DewDropdown> modeBox;

    /** The choice parameter the box edits, pointing into the descriptor's
        static table. Null whenever the type declares no choice. */
    const ParamSpec* modeSpec = nullptr;
    juce::String modeCaption;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectCard)
};

} // namespace dew
