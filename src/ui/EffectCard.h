#pragma once

#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "i18n/Strings.h"

#include "model/EffectType.h"
#include "model/ParamSpec.h"

#include "app/ProjectDocument.h"
#include "ui/EditorState.h"
#include "ui/KnobGrid.h"
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
    static constexpr int modeColumnWidth = 120; ///< fits "Low pass" and the chevron

    /** The drag grip: two columns of dots, and the one thing in the header that
        is not on the icon-button rung because it is not a button. */
    static constexpr int gripWidth = 14;

    /** What the header packs: grip, the disclosure chevron, bypass, icon, name,
        preset and remove. Reorder is the grip and the menu, not two more
        buttons. */
    static constexpr int cardMinWidth = 276 + tokens::size::minTouchTarget;

    /** How tall a card in a ROW is, for a band this many knob rows deep.

        Every card in a row is the same height whatever it holds - cards of
        different heights side by side do not read as a row - so the chain
        quotes this to size its band rather than asking any one card, and a
        card with two parameters is as tall as the one with six beside it.

        It replaced a constant. The band is draggable now, and a card that
        could not answer for a two-row band was the reason dragging it did
        nothing: there was nowhere for the extra height to go.
    */
    static int heightForRows (int knobRows) noexcept;

    EffectCard (EffectChainComponent& owner, ProjectDocument&, EditorState&, juce::ValueTree effect,
                int index);

    int getEffectId() const
    {
        return (int) effect[ids::id];
    }

    bool isExpanded() const;

    void setSelected (bool shouldBeSelected);

    /** What this card's menu offers. Public because the test seam reads it -
        showMenuAsync cannot run headlessly, so every menu in dew is tested as
        a buildMenu/applyMenuChoice pair and the menu is only how a person
        reaches them. The same contract HeaderRow states for the rows.
    */
    juce::PopupMenu buildMenu() const;

    /** Runs the choice the menu returned. Public for the same reason. */
    void applyMenuChoice (int choice);

    /** The rows buildMenu offers, and the ids they carry. Reorder lives here
        rather than in the header because the header had THREE chevrons of two
        shapes in 34 pixels - move-up, move-down, and a collapse caret that was
        the same glyph as one of them whenever the card was open. A caret that
        means two things is a caret that means nothing.
    */
    enum class MenuItem
    {
        moveUp = 1,
        moveDown,
        preset,
        remove
    };

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

    /** How the parameters fall out, in the space this card is given.

        Down a COLUMN the width is the constraint and the rows fall out of it,
        so a narrow sidebar drops a column rather than squeezing three into it.
        Across a ROW it is the other way round: the band's depth is fixed and
        the card is as wide as that leaves it, which is what makes dragging the
        band taller fit more effects across.

        Asked by getRequiredHeight, getRequiredWidth and resized alike, so the
        three cannot disagree the way three hand-mirrored copies of the same
        arithmetic did.
    */
    KnobGrid::Plan gridPlan() const;

    /** What belongs with what: the type's own parameters, then the `mix` every
        effect has.

        Two groups rather than one flat list, because `mix` is the one control
        on every card that is not part of the effect's character - the host
        applies dry/wet identically for every type, which is why the catalog
        keeps it out of the per-type tables. A rule before it says so.
    */
    std::vector<int> groupSizes() const;

    /** The header is identical whichever way the chain runs. It used to differ:
        the reorder arrows pointed along the chain's axis, which is what made
        the vertical case a pile of three chevrons.
    */
    void layOutHeader (juce::Rectangle<int> bounds);

    /** Opens buildMenu at the pointer. Guarded by isOwnPress: this card
        forwards its children's mouse events to itself for hover, and a knob's
        own parameter menu must not arrive with this one on top of it.
    */
    void showMenu (const juce::MouseEvent&);

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

    /** Where a rule goes between two groups sharing a knob row. Decided in
        resized() and drawn in paint(), the split the card's other painted
        regions above already keep. Empty whenever the groups landed on rows of
        their own: the row break is the separation, and a rule as well would be
        saying it twice. */
    std::vector<juce::Rectangle<int>> paramRules;

    /** How many of `params` are the type's own, the rest being the common ones
        the catalog appends. Recorded as they are built rather than counted
        afterwards, because the choice parameter is taken out into modeBox on
        the way past and the two lists no longer line up. */
    int typeParamCount = 0;

    DewIconButton bypassButton { icons::power(), {} };
    DewIconButton expandButton { icons::chevronDown(), tr (StringId::effect_expand_help) };
    DewIconButton presetButton { icons::preset(), tr (StringId::effect_preset_help) };
    DewIconButton removeButton { icons::trash(), tr (StringId::effect_remove_help),
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
