#include "ui/primitives/DewPaint.h"
#include "ui/EffectCard.h"

#include <utility>

#include "i18n/Strings.h"
#include "model/ParamNames.h"
#include "model/Ids.h"
#include "model/ModuleCatalog.h"
#include "model/PresetLibrary.h"
#include "model/ProjectEdits.h"
#include "ui/EffectChainComponent.h"
#include "ui/design/Cursors.h"
#include "ui/design/Gestures.h"
#include "ui/design/Glyphs.h"
#include "ui/design/Icons.h"
#include "ui/design/MenuGlyph.h"
#include "ui/design/ParamPalette.h"

namespace dew
{

using namespace tokens;

EffectCard::EffectCard (EffectChainComponent& o, ProjectDocument& d, EditorState& s,
                        juce::ValueTree e, int i)
    : owner (o)
    , document (d)
    , editorState (s)
    , effect (std::move (e))
    , index (i)
{
    // A card for an effect the catalog does not know cannot be built, and
    // the snapshot builder has already warned about it by the time we are
    // here; falling back keeps the editor usable rather than empty.
    type = effectTypeFor (effect[ids::type].toString()).value_or (EffectType::filter);

    // One effect is one group: what a person means by "skip this effect" and
    // what a screen reader should call the thing it is reading out. The name is
    // the one paint() draws, so the two cannot drift.
    setComponentID ("effectCard");
    setTitle (effectTypeDisplayName (type));
    setFocusContainerType (FocusContainerType::focusContainer);

    bypassButton.setTooltip (tr (StringId::effect_enabled_help));
    bypassButton.setClickingTogglesState (true);
    bypassButton.setToggleState (! (bool) effect[ids::enabled], juce::dontSendNotification);
    bypassButton.setOnColour (colour::warning);
    bypassButton.onClick = [this]
    {
        owner.selectSlot (index);
        ProjectEdits::setProperty (effect, ids::enabled, ! bypassButton.getToggleState(),
                                   &document.getUndoManager(), "Turn effect off");
    };
    addAndMakeVisible (bypassButton);

    expandButton.setComponentID ("effectExpand");
    expandButton.setTooltip (tr (StringId::effect_expand_help));
    expandButton.onClick = [this] { owner.setSlotExpanded (index, ! isExpanded()); };
    addAndMakeVisible (expandButton);

    presetButton.onClick = [this] { owner.showPresetMenu (index, presetButton); };
    presetButton.setEnabled (! PresetLibrary::presetsFor (type).empty());
    addAndMakeVisible (presetButton);

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

bool EffectCard::isExpanded() const
{
    return editorState.isEffectExpanded (getEffectId());
}

void EffectCard::setSelected (bool shouldBeSelected)
{
    if (std::exchange (selected, shouldBeSelected) != shouldBeSelected)
        repaint();
}

// --- sizing ------------------------------------------------------------------

int EffectCard::heightForRows (int knobRows) noexcept
{
    const auto rows = juce::jmax (1, knobRows);

    return size::rowHeight + rows * size::knobRow + (rows - 1) * space::sm + space::sm;
}

std::vector<int> EffectCard::groupSizes() const
{
    const auto own = juce::jlimit (0, params.size(), typeParamCount);
    const auto common = params.size() - own;

    std::vector<int> groups;

    // The type's choice, FIRST and as a group of one - in a ROW only.
    //
    // Across a row it used to be a 120px column of the card's whole height that
    // used one knob row of it, so a four-row band left 222 pixels of empty
    // column beside a single stack of knobs. As a group it packs and wraps like
    // everything else, and the card is as wide as its controls rather than as
    // wide as a reserved column plus its controls.
    //
    // Down a COLUMN it stays a full-width row above the grid. The sidebar is
    // narrow enough that a shared cell width is about seventy pixels, and
    // "Low pass" in seventy pixels is "Low...". The dead column was a row
    // problem; solving it in both places would trade it for a truncation.
    if (modeBox != nullptr && owner.isHorizontal())
        groups.push_back (1);

    if (own > 0)
        groups.push_back (own);

    if (common > 0)
        groups.push_back (common);

    return groups;
}

KnobGrid::Plan EffectCard::gridPlan() const
{
    const auto groups = groupSizes();

    if (owner.isHorizontal())
        return KnobGrid::planForRows (owner.getKnobRowBudget(), groups);

    // The width the chain WILL give this card, asked of the chain rather than
    // read off ourselves: a card's height is wanted before it has been given a
    // width, because the chain sums those heights to answer for its own.
    return KnobGrid::planForWidth (juce::jmax (1, owner.getCardWidth() - 2 * space::sm), groups);
}

int EffectCard::getRequiredHeight() const
{
    if (owner.isHorizontal())
        return heightForRows (owner.getKnobRowBudget());

    if (! isExpanded())
        return size::rowHeight;

    return size::rowHeight + KnobGrid::heightFor (gridPlan()) + space::sm
           + (modeBox != nullptr ? size::controlHeight + space::sm : 0);
}

int EffectCard::getRequiredWidth() const
{
    return juce::jmax (cardMinWidth, KnobGrid::widthFor (gridPlan()) + 2 * space::sm);
}

void EffectCard::refreshValues()
{
    const juce::ScopedValueSetter<bool> quiet (updating, true);

    bypassButton.setToggleState (! (bool) effect[ids::enabled], juce::dontSendNotification);

    // The mode box refreshes with the rest. It did not before, so a filter mode
    // changed by an undo, a preset or an automation write left the box showing
    // what it used to be - which only got easier to hit once a second type had
    // one.
    if (modeBox != nullptr)
        modeBox->setSelectedId (selectedChoiceId(), juce::dontSendNotification);

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

// --- the pointer -------------------------------------------------------------

void EffectCard::mouseDown (const juce::MouseEvent& event)
{
    // Selecting the slot is fine on any button - a menu should act on the card
    // that was clicked, which is the rule the rack and the mixer already state.
    // Everything below it is not: a right press armed the grip reorder, and the
    // release then expanded or collapsed the card.
    owner.selectSlot (index);

    popupPress.down (event, [this, &event] { showMenu (event); });

    if (popupPress.dragging())
    {
        draggingFromGrip = false;
        return;
    }

    // A press on the grip starts a reorder; anywhere else on the header
    // toggles the card, which is the behaviour a header invites.
    const auto local = event.getEventRelativeTo (this).getPosition();

    // The card's own press, never one forwarded from a child - see mouseUp.
    draggingFromGrip = event.originalComponent == this && gripBounds.contains (local);

    // Its own origin, because Component::getDistanceFromDragStart is fed by
    // the real pointer and reads zero in a headless harness - which is why
    // expand-on-click had never been tested.
    pressedAt = local;

    if (draggingFromGrip)
        owner.beginReorder (index, event.getEventRelativeTo (&owner).getPosition());
}

void EffectCard::mouseDrag (const juce::MouseEvent& event)
{
    // forwardChildMouseEventsTo means a drag on a KNOB arrives here too, so
    // the gate is that a grip was pressed - not that the pointer moved.
    if (popupPress.dragging() || ! draggingFromGrip)
        return;

    // Nothing of this card is touched after the call. The chain owns the
    // gesture precisely so that it can rebuild the cards without the one
    // whose handler is on the stack having to survive it.
    owner.updateReorder (event.getEventRelativeTo (&owner).getPosition());
}

void EffectCard::mouseUp (const juce::MouseEvent& event)
{
    if (popupPress.releasing())
        return;

    const auto local = event.getEventRelativeTo (this).getPosition();

    const auto wasGrip = draggingFromGrip;

    // The release has to be the CARD's own. forwardChildMouseEventsTo brings
    // every child's mouse events here too, so without this the chevron folded
    // the card twice - once through its own onClick and once through the header
    // branch below - which is to say not at all, and bypass, preset, up, down
    // and remove each folded it as a silent side effect of being pressed.
    const auto onTheHeader = event.originalComponent == this && local.y < size::rowHeight;

    const auto shouldToggle = ! owner.isHorizontal() && ! wasGrip && onTheHeader
                              && ! gesture::passedThreshold (pressedAt, local);

    draggingFromGrip = false;

    if (shouldToggle)
        owner.setSlotExpanded (index, ! isExpanded());

    // LAST, and everything it needs already read into locals above: a
    // committed reorder deletes this card before the call returns.
    if (wasGrip)
        owner.endReorder (true);
}

void EffectCard::mouseMove (const juce::MouseEvent& event)
{
    // The grip has looked exactly like the rest of the header since it was
    // drawn, which is half of why nobody found the drag.
    const auto overGrip = gripBounds.contains (event.getEventRelativeTo (this).getPosition());

    setMouseCursor (overGrip ? cursor::move : cursor::idle);
}

void EffectCard::mouseEnter (const juce::MouseEvent&)
{
    hover.enter();
}

void EffectCard::mouseExit (const juce::MouseEvent&)
{
    hover.exit();
}

// --- painting and layout -----------------------------------------------------

void EffectCard::paint (juce::Graphics& g)
{
    const auto body = paint::bodyRect (*this);

    // A card, so a chain reads as a stack of things rather than as rows of
    // text floating on the panel behind it.
    g.setColour (colour::surface);
    g.fillRoundedRectangle (body, radius::md);

    auto header = getLocalBounds().removeFromTop (size::rowHeight).toFloat();

    g.setColour (selected ? colour::surfaceHover : colour::surfaceRaised.brighter (hover.lift()));
    g.fillRoundedRectangle (header, radius::md);

    if (showsParameters())
        g.fillRect (header.withTop (header.getBottom() - radius::md));

    const auto bypassed = ! (bool) effect[ids::enabled];

    // What this device DOES, and the same hue its own knobs turn in. A chain of
    // six cards used to be six identical outlines told apart by reading their
    // names; the border and the icon carry it now.
    //
    // Two cards CAN share a colour - filter, EQ and drive are all tone, reverb
    // and delay are both space - because they share a function. That is what
    // the icon is for, and it is why the icon takes the colour at full strength
    // where the border takes it subdued: the border says which family, the icon
    // says which member.
    const auto functionColour = palette::forRole (roleOfEffect (type));
    const auto stated = bypassed ? emphasis::disabled (functionColour) : functionColour;

    // Selection stays accent. It is chrome - it says which card you are talking
    // to, not what the card does - and a selected card that changed hue would
    // be saying both things with one colour.
    g.setColour (selected ? colour::accent : stated.withAlpha (emphasis::subdued));
    g.drawRoundedRectangle (body, radius::md, selected ? stroke::regular : stroke::hairline);

    // A rule under the header, in the card's own colour, but only when the card
    // is OPEN. It is the group-level statement the knobs below make one at a
    // time - and an effect card is the one group in dew that is single-function
    // by construction, so it is the one place such a rule is honest. A closed
    // card is a row in a list and needs no divider inside it.
    if (showsParameters())
    {
        g.setColour (stated.withAlpha (emphasis::subdued));
        g.fillRect (header.withTop (header.getBottom() - stroke::hairline));
    }

    const auto textColour = bypassed ? colour::textDisabled : colour::textPrimary;

    icons::draw (g, icons::grip(), gripBounds.toFloat(), colour::textDisabled);
    icons::draw (g, glyph::forEffect (type), iconBounds.toFloat(),
                 bypassed ? colour::textDisabled : functionColour);

    g.setColour (textColour);
    g.setFont (type::font (type::body));
    g.drawText (effectTypeDisplayName (type), nameBounds, juce::Justification::centredLeft, false);

    if (bypassed)
    {
        g.setColour (colour::warning);
        g.setFont (type::font (type::caption, true));
        g.drawText (tr (StringId::effect_bypassed), nameBounds, juce::Justification::centredRight,
                    false);
    }

    // The mode box is the one control in a row that does not caption
    // itself, so beside captioned knobs it would be the odd one out.
    if (! modeCaptionBounds.isEmpty())
        paint::caption (g, modeCaptionBounds, modeCaption, juce::Justification::centred);

    // What the effect DOES, against the dry/wet that every effect has. Only
    // where the two share a knob row: groups that landed on rows of their own
    // are already separated by the row break, and a rule as well would be
    // saying it twice. Down the row the knobs occupy, which is what the
    // transport bar's group rules do.
    g.setColour (colour::divider);

    for (const auto& rule : paramRules)
        g.drawVerticalLine (rule.getCentreX(), (float) rule.getY(), (float) rule.getBottom());
}

void EffectCard::resized()
{
    layOutHeader (getLocalBounds().removeFromTop (size::rowHeight));

    auto area = getLocalBounds().withTrimmedTop (size::rowHeight).reduced (space::sm, 0);
    const auto visible = showsParameters();

    modeCaptionBounds = {};

    if (modeBox != nullptr)
    {
        modeBox->setVisible (visible);

        // A column puts it above the grid at the card's full width; a row lets
        // the grid place it, as the first cell - see groupSizes.
        if (visible && ! owner.isHorizontal())
        {
            modeBox->setBounds (area.removeFromTop (size::controlHeight));
            area.removeFromTop (space::sm);
        }
    }

    layOutParams (area, visible);
}

bool EffectCard::showsParameters() const
{
    return owner.isHorizontal() || isExpanded();
}

void EffectCard::layOutHeader (juce::Rectangle<int> bounds)
{
    auto header = bounds.reduced (space::xs, space::xxs);

    gripBounds = header.removeFromLeft (gripWidth);
    header.removeFromLeft (space::xxs);

    // The disclosure chevron, on the LEADING edge where a disclosure belongs
    // and where it is the only chevron in the header. It used to sit at the
    // trailing end between two reorder carets, so a collapsed card showed
    // chevron-down twice within 48 pixels meaning two different things.
    expandButton.setVisible (! owner.isHorizontal());

    if (! owner.isHorizontal())
    {
        expandButton.setBounds (header.removeFromLeft (size::iconButton));
        header.removeFromLeft (space::xs);
    }

    bypassButton.setBounds (header.removeFromLeft (size::iconButton));
    header.removeFromLeft (space::xs);
    // The glyph column and the mark inside it, exactly as a menu row spends
    // them - the two were a bare 16 here and a token there, for the same
    // picture at the same size.
    iconBounds = header.removeFromLeft (size::glyphColumn)
                     .withSizeKeepingCentre (size::glyphMark, size::glyphMark);
    header.removeFromLeft (space::xs);

    // Right to left, all at the icon-button rung. Two buttons now, not four:
    // reorder is the grip and the menu. Preset used to be four pixels narrower
    // than bypass and remove beside it, which is not a difference anyone reads
    // as deliberate - it reads as the preset button being somehow lesser.
    removeButton.setBounds (header.removeFromRight (size::iconButton));
    presetButton.setBounds (header.removeFromRight (size::iconButton));
    header.removeFromRight (space::xs);

    nameBounds = header;

    expandButton.setIcon (isExpanded() ? icons::chevronUp() : icons::chevronDown());
}

void EffectCard::layOutParams (juce::Rectangle<int> area, bool visible)
{
    const auto componentFor = [] (ParamWidget& control) -> juce::Component*
    {
        return control.knob != nullptr ? (juce::Component*) control.knob.get()
                                       : (juce::Component*) control.field.get();
    };

    paramRules.clear();

    if (! visible)
    {
        for (auto* control : params)
            componentFor (*control)->setVisible (false);

        modeCaptionBounds = {};
        return;
    }

    // One cell width across every row of the card, and the groups spread
    // across what is left - the grid's business, not this card's. What is
    // still this card's is what a cell HOLDS.
    const auto placed = KnobGrid::place (area, gridPlan());

    // The choice takes the first cell, because groupSizes put it first. Its
    // caption is drawn in the same band a knob's is, so a dropdown in the grid
    // reads as one of the row rather than as a thing parked beside it.
    auto next = (size_t) 0;

    modeCaptionBounds = {};

    if (modeBox != nullptr && owner.isHorizontal())
    {
        if (! placed.cells.empty())
        {
            auto cell = placed.cells[next++];

            modeCaptionBounds = cell.removeFromTop (size::captionBand);
            modeBox->setBounds (cell.withSizeKeepingCentre (cell.getWidth(), size::controlHeight));
        }
    }

    for (int i = 0; i < params.size() && next < placed.cells.size(); ++i, ++next)
    {
        auto* control = params[i];
        auto* component = componentFor (*control);

        component->setVisible (true);

        auto cell = placed.cells[next];

        // A knob fills its cell; a number field is a fixed-height control
        // and stretching it just makes a tall empty box. ASKED rather than
        // declared: this card said 40, the randomize dialog derived 41 and
        // the gallery wrote 40 again, and all three sat beside dropdowns at
        // 26 - three numbers for the one control whose height genuinely
        // varies, and only the control knows how.
        if (control->field != nullptr)
            cell = cell.withSizeKeepingCentre (cell.getWidth(), control->field->preferredHeight());

        component->setBounds (cell);
    }

    paramRules = placed.rules;
}

// --- the controls ------------------------------------------------------------

bool EffectCard::write (const juce::Identifier& property, double value, bool continuing)
{
    if (updating)
        return false;

    ProjectEdits::setProperty (effect, property, value, &document.getUndoManager(),
                               "Change effect parameter", continuing);

    // Whether the caller may advance its gesture. A write refused because a
    // refresh is in progress must not leave a transaction looking open.
    return true;
}

void EffectCard::buildChoice (const ParamSpec& spec)
{
    // A pointer into the DESCRIPTOR's table, which is static, and deliberately
    // not into effectParamsFor's vector, which is a copy that dies at the end of
    // buildParameters. The dropdown outlives that call.
    modeSpec = &spec;

    // From the spec, not from three addItem calls and two triple-ternaries
    // mapping ids to strings by hand. The catalog already carries the choices,
    // their display names and the default - and it had to, because the schema,
    // the file reader and the automation range all read them from there. The
    // editor was the one place that restated them, which is why a second type
    // with a mode had a parameter in the file and no control on the card.
    modeBox = std::make_unique<DewDropdown>();
    modeCaption = tr (paramCaptionOf (*spec.property));

    for (int i = 0; i < spec.numChoices; ++i)
        modeBox->addItem (tr (spec.choices[i].displayName), i + 1);

    modeBox->setSelectedId (selectedChoiceId(), juce::dontSendNotification);

    modeBox->onChange = [this]
    {
        if (updating)
            return;

        const auto choice = juce::jlimit (0, modeSpec->numChoices - 1,
                                          modeBox->getSelectedId() - 1);

        ProjectEdits::setProperty (effect, *modeSpec->property, modeSpec->choices[choice].id,
                                   &document.getUndoManager(), "Change effect mode");
    };

    addAndMakeVisible (*modeBox);
}

int EffectCard::selectedChoiceId() const
{
    const auto stored = effect.getProperty (*modeSpec->property, modeSpec->defaultVar()).toString();

    for (int i = 0; i < modeSpec->numChoices; ++i)
        if (stored == modeSpec->choices[i].id)
            return i + 1;

    return 1;
}

void EffectCard::buildParameters()
{
    const auto& descriptor = effectDescriptor (type);

    for (int i = 0; i < descriptor.numParams; ++i)
    {
        if (descriptor.params[i].control != ParamControl::choice)
            continue;

        // One per type is what the card has room for: the box takes a column of
        // its own beside the knobs. No type declares two, and this says so.
        jassert (modeBox == nullptr);

        if (modeBox == nullptr)
            buildChoice (descriptor.params[i]);
    }

    // Where the type's own parameters end and the common ones begin, which is
    // the group boundary the grid draws a rule at. Counted from the descriptor
    // rather than from the loop below, because the choice is taken out into
    // modeBox on the way past and the two lists stop lining up.
    for (int i = 0; i < descriptor.numParams; ++i)
        if (descriptor.params[i].control != ParamControl::choice)
            ++typeParamCount;

    for (const auto& spec : effectParamsFor (type))
    {
        // A named set is not a number: it gets the dropdown above rather than a
        // knob. One per type is what the card has room for - the box sits in a
        // column of its own beside the knobs - and no type declares two.
        if (spec.control == ParamControl::choice)
            continue;

        auto control = std::make_unique<ParamWidget>();
        control->property = *spec.property;

        const auto property = *spec.property;
        const auto value = (double) effect.getProperty (property, spec.defaultVar());

        if (spec.control == ParamControl::knob)
        {
            // From the SPEC, not from four fields copied out of it. The caption,
            // the range, the step, the decimals, whether it is bipolar and what
            // it DOES all arrive together - and the last of those is why this
            // changed: an effect card's knobs were the only spec-built controls
            // in dew still painting the generic accent, because this was the
            // one place that took the long constructor.
            control->knob = std::make_unique<DewKnob> (spec);
            control->knob->setValue (value, juce::dontSendNotification);

            auto* knob = control->knob.get();
            gesture.attach (*knob, [this, knob, property] (bool continuing)
                            { return write (property, knob->getValue(), continuing); });

            // Built from the same spec that built the knob, so what the
            // menu offers to automate is exactly what the knob turns.
            paramMenu::attachTo (owner.getParamMenuHost(), *knob, [this] { return effect; }, spec);

            addAndMakeVisible (*knob);
        }
        else
        {
            control->field = std::make_unique<DewNumberField>();
            control->field->setRange (spec.minimum, spec.maximum, spec.interval);
            control->field->setNumDecimalPlaces (spec.decimals);
            control->field->setCaption (tr (paramCaptionOf (*spec.property)));
            control->field->setSuffix (spec.suffix);
            control->field->setLogarithmic (spec.curve == ParamCurve::logarithmic);
            control->field->setFunctionColour (palette::forRole (roleOf (*spec.property)));
            control->field->setValue (value, juce::dontSendNotification);

            auto* field = control->field.get();

            // A number field has no edit-end, so its edit is bounded by the
            // start of the next one - which is enough for a field, and was NOT
            // enough across controls. It used to set the knobs' inDrag and
            // never clear it, so after typing a value every later wheel notch
            // on a knob joined the field's undo step. Its own flag now.
            field->onEditStart = [this] { fieldEditing = false; };
            field->onValueChange = [this, field, property]
            {
                if (write (property, field->getValue(), fieldEditing))
                    fieldEditing = true;
            };

            paramMenu::attachTo (owner.getParamMenuHost(), *field, [this] { return effect; }, spec);

            addAndMakeVisible (*field);
        }

        params.add (control.release());
    }
}

} // namespace dew
