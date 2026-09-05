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

    upButton.onClick = [this] { owner.moveSlot (index, index - 1); };
    addAndMakeVisible (upButton);

    downButton.onClick = [this] { owner.moveSlot (index, index + 1); };
    addAndMakeVisible (downButton);

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

int EffectCard::getRequiredHeight() const
{
    if (owner.isHorizontal())
        return cardHeight;

    if (! isExpanded())
        return size::rowHeight;

    const auto rows = (params.size() + columnCount() - 1) / columnCount();
    return size::rowHeight + rows * tokens::size::knobRow + space::sm
           + (modeBox != nullptr ? size::controlHeight + space::sm : 0);
}

int EffectCard::getRequiredWidth() const
{
    const auto mode = modeBox != nullptr ? modeColumnWidth : 0;

    return juce::jmax (cardMinWidth, columnCount() * paramColumnWidth + mode + 2 * space::sm);
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

    popupPress.down (event, nullptr);

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

        if (visible)
        {
            if (owner.isHorizontal())
            {
                // Beside the parameters as one more column, because there
                // is no room above them in a card of fixed height.
                auto cell = area.removeFromLeft (modeColumnWidth)
                                .removeFromTop (tokens::size::knobRow)
                                .reduced (space::xxs, 0);

                modeCaptionBounds = cell.removeFromTop (size::captionBand);
                modeBox->setBounds (
                    cell.withSizeKeepingCentre (cell.getWidth(), size::controlHeight));
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

bool EffectCard::showsParameters() const
{
    return owner.isHorizontal() || isExpanded();
}

int EffectCard::columnCount() const
{
    return owner.isHorizontal() ? juce::jmax (1, params.size()) : columns;
}

void EffectCard::layOutHeader (juce::Rectangle<int> bounds)
{
    auto header = bounds.reduced (space::xs, space::xxs);

    gripBounds = header.removeFromLeft (14);
    header.removeFromLeft (space::xxs);
    bypassButton.setBounds (header.removeFromLeft (size::iconButton));
    header.removeFromLeft (space::xs);
    iconBounds = header.removeFromLeft (16).withSizeKeepingCentre (16, 16);
    header.removeFromLeft (space::xs);

    // Right to left, all at the icon-button rung. Preset, up and down used
    // to be four pixels narrower than bypass and remove beside them, which
    // is not a difference anyone reads as deliberate - it reads as the
    // preset button being somehow lesser than the ones it sits between.
    removeButton.setBounds (header.removeFromRight (size::iconButton));
    presetButton.setBounds (header.removeFromRight (size::iconButton));
    downButton.setBounds (header.removeFromRight (size::iconButton));
    upButton.setBounds (header.removeFromRight (size::iconButton));
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

void EffectCard::layOutParams (juce::Rectangle<int> area, bool visible)
{
    const auto columnsHere = columnCount();

    for (int i = 0; i < params.size(); i += columnsHere)
    {
        auto row = visible ? area.removeFromTop (tokens::size::knobRow) : juce::Rectangle<int>();
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

            // A knob fills its cell; a number field is a fixed-height control
            // and stretching it just makes a tall empty box. ASKED rather than
            // declared: this card said 40, the randomize dialog derived 41 and
            // the gallery wrote 40 again, and all three sat beside dropdowns at
            // 26 - three numbers for the one control whose height genuinely
            // varies, and only the control knows how.
            if (control->field != nullptr)
                cell = cell.withSizeKeepingCentre (cell.getWidth(),
                                                   control->field->preferredHeight());

            component->setBounds (cell);
        }
    }
}

// --- the controls ------------------------------------------------------------

void EffectCard::write (const juce::Identifier& property, double value)
{
    if (updating)
        return;

    ProjectEdits::setProperty (effect, property, value, &document.getUndoManager(),
                               "Change effect parameter", gestureActive);

    gestureActive = inDrag;
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
            knob->onEditStart = [this]
            {
                inDrag = true;
                gestureActive = false;
            };
            knob->onEditEnd = [this]
            {
                inDrag = false;
                gestureActive = false;
            };
            knob->onValueChange = [this, knob, property] { write (property, knob->getValue()); };

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
            // A number field has no edit-end, so its drag is bounded by the
            // start of the next one - which is enough: a new gesture opens
            // its own transaction either way.
            field->onEditStart = [this]
            {
                inDrag = true;
                gestureActive = false;
            };
            field->onValueChange = [this, field, property] { write (property, field->getValue()); };

            paramMenu::attachTo (owner.getParamMenuHost(), *field, [this] { return effect; }, spec);

            addAndMakeVisible (*field);
        }

        params.add (control.release());
    }
}

} // namespace dew
