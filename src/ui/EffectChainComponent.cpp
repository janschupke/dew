#include "EffectChainComponent.h"

#include "engine/Effects.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "model/ProjectSchema.h"
#include "design/Icons.h"
#include "design/Tokens.h"

namespace dew
{

using namespace tokens;

namespace
{

juce::Path iconForType (EffectType type)
{
    switch (type)
    {
        case EffectType::reverb: return icons::effectReverb();
        case EffectType::delay:  return icons::effectDelay();
        case EffectType::drive:  return icons::effectDrive();
        case EffectType::chorus: return icons::effectChorus();
        case EffectType::eq:     return icons::effectEq();
        case EffectType::filter: break;
    }

    return icons::effectFilter();
}

/** How one parameter is presented.

    `knob` for the 0..1 quantities a hand turns, `field` for frequencies and
    times where the number itself matters. A table rather than a switch full of
    setter calls: adding an effect type is a row here, and the layout code never
    has to know what a "cutoff" is.
*/
struct ParamSpec
{
    enum class Control { knob, field };

    const juce::Identifier& property;
    const char* caption;
    double minimum, maximum, interval;
    int decimals;
    const char* suffix;
    Control control;
    bool bipolar = false;
    bool logarithmic = false;
};

std::vector<ParamSpec> parametersFor (EffectType type)
{
    using C = ParamSpec::Control;

    switch (type)
    {
        case EffectType::filter:
            return { { ids::cutoff,     "CUTOFF", 20.0, 18000.0, 1.0, 0, " Hz", C::field, false, true },
                     { ids::resonance,  "RES",    0.05, 4.0,   0.01, 2, "", C::knob },
                     { ids::mix,        "MIX",    0.0,  1.0,   0.01, 2, "", C::knob } };

        case EffectType::reverb:
            return { { ids::roomSize,   "SIZE",   0.0,  1.0,   0.01, 2, "", C::knob },
                     { ids::damping,    "DAMP",   0.0,  1.0,   0.01, 2, "", C::knob },
                     { ids::width,      "WIDTH",  0.0,  1.0,   0.01, 2, "", C::knob },
                     { ids::mix,        "MIX",    0.0,  1.0,   0.01, 2, "", C::knob } };

        case EffectType::delay:
            return { { ids::delayMs,    "TIME",   1.0,  1000.0, 1.0, 0, " ms", C::field, false, true },
                     { ids::feedback,   "FBK",    0.0,  0.95,  0.01, 2, "", C::knob },
                     { ids::mix,        "MIX",    0.0,  1.0,   0.01, 2, "", C::knob } };

        case EffectType::drive:
            return { { ids::drive,      "DRIVE",  1.0,  40.0,  0.1,  1, "", C::knob },
                     { ids::outputGain, "OUT",    0.0,  4.0,   0.01, 2, "", C::knob },
                     { ids::mix,        "MIX",    0.0,  1.0,   0.01, 2, "", C::knob } };

        case EffectType::chorus:
            return { { ids::rate,       "RATE",   0.01, 20.0,  0.01, 2, " Hz", C::field, false, true },
                     { ids::depth,      "DEPTH",  0.0,  1.0,   0.01, 2, "", C::knob },
                     { ids::mix,        "MIX",    0.0,  1.0,   0.01, 2, "", C::knob } };

        case EffectType::eq:
            return { { ids::lowGainDb,  "LOW",    -24.0, 24.0, 0.1,  1, " dB", C::knob, true },
                     { ids::midGainDb,  "MID",    -24.0, 24.0, 0.1,  1, " dB", C::knob, true },
                     { ids::highGainDb, "HIGH",   -24.0, 24.0, 0.1,  1, " dB", C::knob, true },
                     { ids::midFreq,    "FREQ",   100.0, 8000.0, 1.0, 0, " Hz", C::field, false, true } };
    }

    return {};
}

const EffectType allEffectTypes[] { EffectType::filter, EffectType::reverb, EffectType::delay,
                                    EffectType::drive, EffectType::chorus, EffectType::eq };

} // namespace

/** One effect: a header that is always visible, and a body that folds away. */
class EffectChainComponent::Card : public juce::Component
{
public:
    static constexpr int columns = 3;            ///< down a column
    static constexpr int paramRowHeight = 68;
    static constexpr int numberFieldHeight = 40;
    static constexpr int captionHeight = 12;

    // A column is sized rather than stretched: a number field wider than a hand
    // is not easier to drag, only emptier. 88 is what the gallery gives a knob
    // (72 wide) and a number field (86) with room for the cell inset.
    static constexpr int paramColumnWidth = 88;
    static constexpr int modeColumnWidth = 120;  ///< fits "Low pass" and the chevron
    static constexpr int cardMinWidth = 276;     ///< what the header packs

    /** Every card in a row is this tall. The chain quotes it to size the band. */
    static constexpr int cardHeight = size::rowHeight + paramRowHeight + space::sm;

    Card (EffectChainComponent& o, ProjectDocument& d, EditorState& s, juce::ValueTree e, int i)
        : owner (o), document (d), editorState (s), effect (std::move (e)), index (i)
    {
        type = effectTypeFromString (effect[ids::type].toString());

        bypassButton.setClickingTogglesState (true);
        bypassButton.setToggleState (! (bool) effect[ids::enabled], juce::dontSendNotification);
        bypassButton.setOnColour (colour::warning);
        bypassButton.onClick = [this]
        {
            owner.selectSlot (index);
            auto& undo = document.getUndoManager();
            undo.beginNewTransaction ("Bypass effect");
            effect.setProperty (ids::enabled, ! bypassButton.getToggleState(), &undo);
        };
        addAndMakeVisible (bypassButton);

        expandButton.setTooltip ("Show or hide this effect's controls");
        expandButton.onClick = [this] { owner.setSlotExpanded (index, ! isExpanded()); };
        addAndMakeVisible (expandButton);

        upButton.onClick = [this] { owner.moveSlot (index, index - 1); };
        addAndMakeVisible (upButton);

        downButton.onClick = [this] { owner.moveSlot (index, index + 1); };
        addAndMakeVisible (downButton);

        removeButton.onClick = [this]
        {
            auto& undo = document.getUndoManager();
            undo.beginNewTransaction ("Remove effect");
            ProjectEdits::removeEffect (owner.getOwner(), effect, &undo);
        };
        addAndMakeVisible (removeButton);

        buildParameters();

        forwardChildMouseEventsTo (*this);
    }

    int getEffectId() const { return (int) effect[ids::id]; }
    bool isExpanded() const { return editorState.isEffectExpanded (getEffectId()); }

    void setSelected (bool shouldBeSelected)
    {
        if (std::exchange (selected, shouldBeSelected) != shouldBeSelected)
            repaint();
    }

    /** Header plus, when open, the parameters.

        In a row every card is the same height whatever it holds. Cards of
        different heights side by side do not read as a row, and nothing folds
        there anyway.
    */
    int getRequiredHeight() const
    {
        if (owner.isHorizontal())
            return cardHeight;

        if (! isExpanded())
            return size::rowHeight;

        const auto rows = (params.size() + columnCount() - 1) / columnCount();
        return size::rowHeight + rows * paramRowHeight + space::sm
               + (modeBox != nullptr ? size::controlHeight + space::sm : 0);
    }

    /** How wide the card has to be for its header and its one row of controls.

        Only meaningful in a row; in a column a card is given the chain's width.
        The floor is what the header packs: the grip, bypass and type icon on
        the left, reorder and remove on the right, and enough between them for
        the effect's name beside a "BYPASSED" that is drawn into the same space.
    */
    int getRequiredWidth() const
    {
        const auto mode = modeBox != nullptr ? modeColumnWidth : 0;

        return juce::jmax (cardMinWidth,
                           columnCount() * paramColumnWidth + mode + 2 * space::sm);
    }

    void refreshValues()
    {
        const juce::ScopedValueSetter<bool> quiet (updating, true);

        bypassButton.setToggleState (! (bool) effect[ids::enabled], juce::dontSendNotification);

        for (auto* control : params)
        {
            const auto value = (double) effect[control->property];

            if (control->knob != nullptr)
                control->knob->setValue (value, juce::dontSendNotification);
            else if (control->field != nullptr)
                control->field->setValue (value, juce::dontSendNotification);
        }

        repaint();
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        owner.selectSlot (index);

        // A press on the grip starts a reorder; anywhere else on the header
        // toggles the card, which is the behaviour a header invites.
        draggingFromGrip = gripBounds.contains (event.getEventRelativeTo (this).getPosition());
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (! draggingFromGrip)
            return;

        // Which card the cursor is over, not how far it has travelled divided
        // by a row height: that divisor was size::rowHeight even though an open
        // card is three times that tall, so dragging a card by its own height
        // moved it three places. And in a row the cards are not even all the
        // same width, so there is no divisor to use.
        const auto target = owner.slotAtPosition (event.getEventRelativeTo (&owner).getPosition());

        if (target != index)
        {
            owner.moveSlot (index, target);
            draggingFromGrip = false;      // the cards were rebuilt under us
        }
    }

    void mouseUp (const juce::MouseEvent& event) override
    {
        const auto local = event.getEventRelativeTo (this).getPosition();

        if (! owner.isHorizontal() && ! draggingFromGrip && local.y < size::rowHeight
            && event.getDistanceFromDragStart() < 4)
            owner.setSlotExpanded (index, ! isExpanded());

        draggingFromGrip = false;
    }

    void mouseEnter (const juce::MouseEvent&) override { setHovered (true); }
    void mouseExit (const juce::MouseEvent&) override  { setHovered (isMouseOver (true)); }

    void paint (juce::Graphics& g) override
    {
        const auto body = getLocalBounds().toFloat().reduced (0.5f);

        // A card, so a chain reads as a stack of things rather than as rows of
        // text floating on the panel behind it.
        g.setColour (colour::surface);
        g.fillRoundedRectangle (body, radius::md);

        auto header = getLocalBounds().removeFromTop (size::rowHeight).toFloat();

        g.setColour (selected ? colour::surfaceHover
                              : hovered ? colour::surfaceRaised.brighter (0.05f)
                                        : colour::surfaceRaised);
        g.fillRoundedRectangle (header, radius::md);

        if (showsParameters())
            g.fillRect (header.withTop (header.getBottom() - radius::md));

        g.setColour (selected ? colour::accent : colour::outline);
        g.drawRoundedRectangle (body, radius::md,
                                selected ? stroke::regular : stroke::hairline);

        const auto bypassed = ! (bool) effect[ids::enabled];
        const auto textColour = bypassed ? colour::textDisabled : colour::textPrimary;

        icons::draw (g, icons::grip(), gripBounds.toFloat(), colour::textDisabled);
        icons::draw (g, iconForType (type), iconBounds.toFloat(), textColour);

        g.setColour (textColour);
        g.setFont (type::font (type::body));
        g.drawText (effectTypeDisplayName (type), nameBounds,
                    juce::Justification::centredLeft, false);

        if (bypassed)
        {
            g.setColour (colour::warning);
            g.setFont (type::font (type::caption, true));
            g.drawText ("BYPASSED", nameBounds, juce::Justification::centredRight, false);
        }

        // The mode box is the one control in a row that does not caption
        // itself, so beside captioned knobs it would be the odd one out.
        if (! modeCaptionBounds.isEmpty())
            paint::caption (g, modeCaptionBounds, "MODE", juce::Justification::centred);
    }

    void resized() override
    {
        layOutHeader (getLocalBounds().removeFromTop (size::rowHeight));

        auto area = getLocalBounds().withTrimmedTop (size::rowHeight).reduced (space::sm, 0);
        const auto visible = showsParameters();

        modeCaptionBounds = {};

        if (modeBox != nullptr)
        {
            modeBox->setVisible (visible);

            if (visible)
            {
                if (owner.isHorizontal())
                {
                    // Beside the parameters as one more column, because there
                    // is no room above them in a card of fixed height.
                    auto cell = area.removeFromLeft (modeColumnWidth)
                                    .removeFromTop (paramRowHeight)
                                    .reduced (space::xxs, 0);

                    modeCaptionBounds = cell.removeFromTop (captionHeight);
                    modeBox->setBounds (cell.withSizeKeepingCentre (cell.getWidth(),
                                                                    size::controlHeight));
                }
                else
                {
                    modeBox->setBounds (area.removeFromTop (size::controlHeight));
                    area.removeFromTop (space::sm);
                }
            }
        }

        layOutParams (area, visible);
    }

private:
    /** Whether the parameters are showing. Folding is a column behaviour; in a
        row every card is open, so there is nothing for the chevron to do.
    */
    bool showsParameters() const { return owner.isHorizontal() || isExpanded(); }

    /** Three to a row down a column, everything on one row across a band.

        jmax because parametersFor() has a fallback that returns nothing, and a
        column count of zero is both an infinite loop and a divide by zero in
        layOutParams.
    */
    int columnCount() const
    {
        return owner.isHorizontal() ? juce::jmax (1, params.size()) : columns;
    }

    /** The header is identical whichever way the chain runs - only the reorder
        arrows change, because they point the way the chain goes.
    */
    void layOutHeader (juce::Rectangle<int> bounds)
    {
        auto header = bounds.reduced (space::xs, space::xxs);

        gripBounds = header.removeFromLeft (14);
        header.removeFromLeft (space::xxs);
        bypassButton.setBounds (header.removeFromLeft (size::minTouchTarget + 4));
        header.removeFromLeft (space::xs);
        iconBounds = header.removeFromLeft (16).withSizeKeepingCentre (16, 16);
        header.removeFromLeft (space::xs);

        // Right to left, at the documented minimum touch target - the old row
        // packed four 18px buttons into 24px of height.
        removeButton.setBounds (header.removeFromRight (size::minTouchTarget + 4));
        downButton.setBounds (header.removeFromRight (size::minTouchTarget));
        upButton.setBounds (header.removeFromRight (size::minTouchTarget));
        header.removeFromRight (space::xs);

        expandButton.setVisible (! owner.isHorizontal());

        if (! owner.isHorizontal())
        {
            expandButton.setBounds (header.removeFromRight (size::minTouchTarget + 4));
            header.removeFromRight (space::sm);
        }

        nameBounds = header;

        expandButton.setIcon (isExpanded() ? icons::chevronUp() : icons::chevronDown());

        upButton.setIcon (owner.isHorizontal() ? icons::chevronLeft() : icons::chevronUp());
        downButton.setIcon (owner.isHorizontal() ? icons::chevronRight() : icons::chevronDown());
    }

    /** The parameter grid. Shared by both orientations: they differ only in how
        many columns there are and in the rectangle they hand it.
    */
    void layOutParams (juce::Rectangle<int> area, bool visible)
    {
        const auto columnsHere = columnCount();

        for (int i = 0; i < params.size(); i += columnsHere)
        {
            auto row = visible ? area.removeFromTop (paramRowHeight) : juce::Rectangle<int>();
            const auto width = juce::jmax (1, row.getWidth() / columnsHere);

            for (int c = 0; c < columnsHere && i + c < params.size(); ++c)
            {
                auto* control = params[i + c];
                auto* component = control->knob != nullptr ? (juce::Component*) control->knob.get()
                                                           : (juce::Component*) control->field.get();
                component->setVisible (visible);

                if (! visible)
                    continue;

                auto cell = row.removeFromLeft (width).reduced (space::xxs);

                // A knob fills its cell; a number field is a fixed-height
                // control and stretching it just makes a tall empty box.
                if (control->field != nullptr)
                    cell = cell.withSizeKeepingCentre (cell.getWidth(), numberFieldHeight);

                component->setBounds (cell);
            }
        }
    }

    struct ParamControl
    {
        std::unique_ptr<DewKnob> knob;
        std::unique_ptr<DewNumberField> field;
        juce::Identifier property;
    };

    void setHovered (bool shouldBeHovered)
    {
        if (std::exchange (hovered, shouldBeHovered) != shouldBeHovered)
            repaint();
    }

    void write (const juce::Identifier& property, double value)
    {
        if (updating)
            return;

        auto mutableEffect = effect;
        mutableEffect.setProperty (property, value, &document.getUndoManager());
    }

    void buildParameters()
    {
        if (type == EffectType::filter)
        {
            modeBox = std::make_unique<juce::ComboBox>();
            modeBox->addItem ("Low pass", 1);
            modeBox->addItem ("High pass", 2);
            modeBox->addItem ("Band pass", 3);

            const auto mode = filterModeFromString (effect[ids::filterMode].toString());
            modeBox->setSelectedId (mode == FilterMode::lowpass ? 1
                                    : mode == FilterMode::highpass ? 2 : 3,
                                    juce::dontSendNotification);

            modeBox->onChange = [this]
            {
                if (updating)
                    return;

                auto& undo = document.getUndoManager();
                undo.beginNewTransaction ("Change filter mode");
                auto mutableEffect = effect;
                mutableEffect.setProperty (ids::filterMode,
                                           modeBox->getSelectedId() == 2 ? "highpass"
                                           : modeBox->getSelectedId() == 3 ? "bandpass" : "lowpass",
                                           &undo);
            };

            addAndMakeVisible (*modeBox);
        }

        for (const auto& spec : parametersFor (type))
        {
            auto control = std::make_unique<ParamControl>();
            control->property = spec.property;

            const auto property = spec.property;
            const auto value = (double) effect[property];

            if (spec.control == ParamSpec::Control::knob)
            {
                control->knob = std::make_unique<DewKnob> (spec.caption, spec.minimum,
                                                           spec.maximum, spec.interval);
                control->knob->setNumDecimalPlaces (spec.decimals);
                control->knob->setBipolar (spec.bipolar);
                control->knob->setValue (value, juce::dontSendNotification);

                auto* knob = control->knob.get();
                knob->onEditStart = [this] { document.getUndoManager().beginNewTransaction ("Change effect"); };
                knob->onValueChange = [this, knob, property] { write (property, knob->getValue()); };
                addAndMakeVisible (*knob);
            }
            else
            {
                control->field = std::make_unique<DewNumberField>();
                control->field->setRange (spec.minimum, spec.maximum, spec.interval);
                control->field->setNumDecimalPlaces (spec.decimals);
                control->field->setCaption (spec.caption);
                control->field->setSuffix (spec.suffix);
                control->field->setLogarithmic (spec.logarithmic);
                control->field->setValue (value, juce::dontSendNotification);

                auto* field = control->field.get();
                field->onEditStart = [this] { document.getUndoManager().beginNewTransaction ("Change effect"); };
                field->onValueChange = [this, field, property] { write (property, field->getValue()); };
                addAndMakeVisible (*field);
            }

            params.add (control.release());
        }
    }

    EffectChainComponent& owner;
    ProjectDocument& document;
    EditorState& editorState;
    juce::ValueTree effect;
    int index = 0;
    EffectType type = EffectType::filter;

    bool selected = false;
    bool hovered = false;
    bool updating = false;
    bool draggingFromGrip = false;

    juce::Rectangle<int> gripBounds, iconBounds, nameBounds, modeCaptionBounds;

    DewIconButton bypassButton { icons::power(), "Bypass this effect" };
    DewIconButton expandButton { icons::chevronDown(), "Show or hide this effect's controls" };
    DewIconButton upButton { icons::chevronUp(), "Move earlier in the chain" };
    DewIconButton downButton { icons::chevronDown(), "Move later in the chain" };
    DewIconButton removeButton { icons::trash(), "Remove this effect" };

    juce::OwnedArray<ParamControl> params;
    std::unique_ptr<juce::ComboBox> modeBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Card)
};

// -----------------------------------------------------------------------------

EffectChainComponent::EffectChainComponent (ProjectDocument& d, EditorState& s)
    : document (d), editorState (s)
{
    setComponentID ("effectChain");

    document.getState().addListener (this);
    editorState.addChangeListener (this);
}

EffectChainComponent::~EffectChainComponent()
{
    editorState.removeChangeListener (this);
    document.getState().removeListener (this);
}

void EffectChainComponent::setOwner (juce::ValueTree owner)
{
    if (chainOwner == owner)
        return;

    chainOwner = std::move (owner);
    selectedSlot = 0;
    rebuild();
}

void EffectChainComponent::setOrientation (Orientation wanted)
{
    if (std::exchange (orientation, wanted) == wanted)
        return;

    for (auto* card : cards)
        card->resized();

    resized();
    repaint();
    notifyRequiredSizeChanged();
}

bool EffectChainComponent::canAddEffect() const
{
    return chainOwner.isValid() && cards.size() < kMaxEffectsPerChain;
}

juce::ValueTree EffectChainComponent::effectAt (int index) const
{
    int i = 0;

    for (const auto& child : chainOwner)
        if (child.hasType (ids::EFFECT) && i++ == index)
            return child;

    return {};
}

void EffectChainComponent::selectSlot (int index)
{
    const auto count = ProjectEdits::countEffects (chainOwner);
    const auto wanted = juce::jlimit (0, juce::jmax (0, count - 1), index);

    if (wanted == selectedSlot)
        return;

    selectedSlot = wanted;

    for (int i = 0; i < cards.size(); ++i)
        cards[i]->setSelected (i == selectedSlot);
}

bool EffectChainComponent::isSlotExpanded (int index) const
{
    const auto effect = effectAt (index);
    return effect.isValid() && editorState.isEffectExpanded ((int) effect[ids::id]);
}

void EffectChainComponent::setSlotExpanded (int index, bool expanded)
{
    if (const auto effect = effectAt (index); effect.isValid())
        editorState.setEffectExpanded ((int) effect[ids::id], expanded);
}

void EffectChainComponent::moveSlot (int from, int to)
{
    const auto count = ProjectEdits::countEffects (chainOwner);
    const auto target = juce::jlimit (0, juce::jmax (0, count - 1), to);

    if (from == target)
        return;

    auto effect = effectAt (from);

    if (! effect.isValid())
        return;

    auto& undo = document.getUndoManager();
    undo.beginNewTransaction ("Reorder effects");
    ProjectEdits::moveEffect (chainOwner, effect, target, &undo);
    selectedSlot = target;
}

void EffectChainComponent::addEffectOfType (const juce::String& type)
{
    if (! chainOwner.isValid())
        return;

    auto& undo = document.getUndoManager();
    undo.beginNewTransaction ("Add effect");

    const auto added = ProjectEdits::addEffect (document.getState(), chainOwner, type, &undo);

    if (! added.isValid())
        return;

    selectedSlot = ProjectEdits::countEffects (chainOwner) - 1;

    // A new effect opens: you added it to set it up.
    editorState.setEffectExpanded ((int) added[ids::id], true);
}

void EffectChainComponent::showAddMenu (juce::Component& target)
{
    if (ProjectEdits::countEffects (chainOwner) >= kMaxEffectsPerChain)
        return;

    juce::PopupMenu menu;

    for (int i = 0; i < (int) std::size (allEffectTypes); ++i)
        menu.addItem (i + 1, effectTypeDisplayName (allEffectTypes[i]));

    menu.setLookAndFeel (&getLookAndFeel());
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target),
                        [this] (int choice)
                        {
                            if (choice > 0 && choice <= (int) std::size (allEffectTypes))
                                addEffectOfType (effectTypeToString (allEffectTypes[choice - 1]));
                        });
}

void EffectChainComponent::rebuild()
{
    const juce::ScopedValueSetter<bool> quiet (rebuilding, true);

    cards.clear();

    int index = 0;

    for (const auto& child : chainOwner)
        if (child.hasType (ids::EFFECT))
            addAndMakeVisible (cards.add (new Card (*this, document, editorState, child, index++)));

    selectedSlot = juce::jlimit (0, juce::jmax (0, cards.size() - 1), selectedSlot);

    // With nothing open, the panel shows only a list of names and no controls -
    // strictly less than the single-selection editor this replaced. So the
    // selected card opens unless the user has opened something themselves.
    auto anyExpanded = false;

    for (auto* card : cards)
        anyExpanded = anyExpanded || card->isExpanded();

    if (! anyExpanded && ! cards.isEmpty())
        editorState.setEffectExpanded (cards[selectedSlot]->getEffectId(), true);

    for (int i = 0; i < cards.size(); ++i)
        cards[i]->setSelected (i == selectedSlot);

    resized();
    repaint();
    notifyRequiredSizeChanged();
}

int EffectChainComponent::getRequiredHeight() const
{
    // A row is one card tall whether it holds four effects or none, so the
    // mixer's effect band does not change height as you fill it.
    if (isHorizontal())
        return Card::cardHeight + space::xs + space::sm;

    auto height = 0;

    for (auto* card : cards)
        height += card->getRequiredHeight() + space::xs;

    if (cards.isEmpty())
        height += size::rowHeight;

    return height + space::sm;
}

int EffectChainComponent::getRequiredWidth() const
{
    if (! isHorizontal())
        return getWidth();

    auto width = space::xs;

    for (auto* card : cards)
        width += card->getRequiredWidth() + space::xs;

    return width;
}

int EffectChainComponent::slotAtPosition (juce::Point<int> position) const
{
    for (int i = 0; i < cards.size(); ++i)
        if (isHorizontal() ? position.x < cards[i]->getRight()
                           : position.y < cards[i]->getBottom())
            return i;

    return juce::jmax (0, cards.size() - 1);
}

void EffectChainComponent::notifyRequiredSizeChanged()
{
    if (onRequiredSizeChanged != nullptr)
        onRequiredSizeChanged();
}

void EffectChainComponent::resized()
{
    auto area = getLocalBounds();

    if (isHorizontal())
    {
        // The band is one card tall; the cards run across it and the viewport
        // scrolls sideways when there are more than fit.
        auto row = area.removeFromTop (Card::cardHeight);
        row.removeFromLeft (space::xs);

        for (auto* card : cards)
        {
            card->setBounds (row.removeFromLeft (card->getRequiredWidth()));
            row.removeFromLeft (space::xs);
        }

        return;
    }

    for (auto* card : cards)
    {
        card->setBounds (area.removeFromTop (card->getRequiredHeight()));
        area.removeFromTop (space::xs);
    }
}

void EffectChainComponent::paint (juce::Graphics& g)
{
    if (! chainOwner.isValid())
    {
        paint::emptyState (g, getLocalBounds().removeFromTop (size::rowHeight * 2),
                           "Nothing selected");
        return;
    }

    if (cards.isEmpty())
    {
        // Across the viewport, not across the content: with no cards the two
        // are the same, and this way the message cannot start off-screen.
        const juce::Rectangle<int> empty { 0, 0, getWidth(), size::rowHeight };

        paint::inertArea (g, empty);

        paint::emptyState (g, empty.reduced (space::md, 0), "No effects yet - use + to add one",
                           juce::Justification::centredLeft);
    }
}

void EffectChainComponent::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
{
    if (parent == chainOwner || child.hasType (ids::EFFECT))
        rebuild();
}

void EffectChainComponent::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int)
{
    if (parent == chainOwner || child.hasType (ids::EFFECT))
        rebuild();
}

void EffectChainComponent::valueTreeChildOrderChanged (juce::ValueTree& parent, int, int)
{
    if (parent == chainOwner)
        rebuild();
}

void EffectChainComponent::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&)
{
    if (! tree.hasType (ids::EFFECT))
        return;

    for (auto* card : cards)
        if (card->getEffectId() == (int) tree[ids::id])
            card->refreshValues();
}

void EffectChainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // A card opening or closing changes every card's position and the chain's
    // height, so the host that scrolls it has to be told.
    for (auto* card : cards)
        card->resized();

    resized();
    repaint();
    notifyRequiredSizeChanged();
}

} // namespace dew
