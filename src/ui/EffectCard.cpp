#include "ui/EffectCard.h"

#include <utility>

#include "model/Ids.h"
#include "model/ModuleCatalog.h"
#include "model/PresetLibrary.h"
#include "model/ProjectEdits.h"
#include "ui/EffectChainComponent.h"
#include "ui/design/Cursors.h"
#include "ui/design/Gestures.h"
#include "ui/design/Icons.h"

namespace dew
{

using namespace tokens;

namespace
{

juce::Path iconForType (EffectType type)
{
    // Every case listed and nothing after the switch, so a new effect type is a
    // compile error here rather than a card that silently wears the filter's
    // icon. It was the last of the four things that used to go quietly wrong
    // when someone added a type.
    switch (type)
    {
        case EffectType::filter: return icons::effectFilter();
        case EffectType::reverb: return icons::effectReverb();
        case EffectType::delay: return icons::effectDelay();
        case EffectType::drive: return icons::effectDrive();
        case EffectType::chorus: return icons::effectChorus();
        case EffectType::eq: return icons::effectEq();
    }

    jassertfalse;
    return {};
}

} // namespace

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

    bypassButton.setClickingTogglesState (true);
    bypassButton.setToggleState (! (bool) effect[ids::enabled], juce::dontSendNotification);
    bypassButton.setOnColour (colour::warning);
    bypassButton.onClick = [this]
    {
        owner.selectSlot (index);
        ProjectEdits::setProperty (effect, ids::enabled, ! bypassButton.getToggleState(),
                                   &document.getUndoManager(), "Bypass effect");
    };
    addAndMakeVisible (bypassButton);

    expandButton.setTooltip ("Show or hide this effect's controls");
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
    owner.selectSlot (index);

    // A press on the grip starts a reorder; anywhere else on the header
    // toggles the card, which is the behaviour a header invites.
    const auto local = event.getEventRelativeTo (this).getPosition();

    draggingFromGrip = gripBounds.contains (local);

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
    if (! draggingFromGrip)
        return;

    // Nothing of this card is touched after the call. The chain owns the
    // gesture precisely so that it can rebuild the cards without the one
    // whose handler is on the stack having to survive it.
    owner.updateReorder (event.getEventRelativeTo (&owner).getPosition());
}

void EffectCard::mouseUp (const juce::MouseEvent& event)
{
    const auto local = event.getEventRelativeTo (this).getPosition();

    const auto wasGrip = draggingFromGrip;
    const auto shouldToggle = ! owner.isHorizontal() && ! wasGrip && local.y < size::rowHeight
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

    g.setColour (selected ? colour::accent : colour::outline);
    g.drawRoundedRectangle (body, radius::md, selected ? stroke::regular : stroke::hairline);

    const auto bypassed = ! (bool) effect[ids::enabled];
    const auto textColour = bypassed ? colour::textDisabled : colour::textPrimary;

    icons::draw (g, icons::grip(), gripBounds.toFloat(), colour::textDisabled);
    icons::draw (g, iconForType (type), iconBounds.toFloat(), textColour);

    g.setColour (textColour);
    g.setFont (type::font (type::body));
    g.drawText (effectTypeDisplayName (type), nameBounds, juce::Justification::centredLeft, false);

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

                modeCaptionBounds = cell.removeFromTop (captionHeight);
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

            // A knob fills its cell; a number field is a fixed-height
            // control and stretching it just makes a tall empty box.
            if (control->field != nullptr)
                cell = cell.withSizeKeepingCentre (cell.getWidth(), numberFieldHeight);

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

void EffectCard::buildParameters()
{
    if (type == EffectType::filter)
    {
        modeBox = std::make_unique<DewDropdown>();
        modeBox->addItem ("Low pass", 1);
        modeBox->addItem ("High pass", 2);
        modeBox->addItem ("Band pass", 3);

        const auto mode = filterModeFromString (effect[ids::filterMode].toString());
        modeBox->setSelectedId (mode == FilterMode::lowpass    ? 1
                                : mode == FilterMode::highpass ? 2
                                                               : 3,
                                juce::dontSendNotification);

        modeBox->onChange = [this]
        {
            if (updating)
                return;

            ProjectEdits::setProperty (effect, ids::filterMode,
                                       modeBox->getSelectedId() == 2   ? "highpass"
                                       : modeBox->getSelectedId() == 3 ? "bandpass"
                                                                       : "lowpass",
                                       &document.getUndoManager(), "Change filter mode");
        };

        addAndMakeVisible (*modeBox);
    }

    for (const auto& spec : effectParamsFor (type))
    {
        // The filter's mode is a named set, not a number, and it has its own
        // combo box above. Everything else the catalog declares gets a
        // control here - which is how the EQ finally shows its mix, a
        // parameter the engine has always applied and this editor never
        // offered.
        if (spec.control == ParamControl::choice)
            continue;

        auto control = std::make_unique<ParamWidget>();
        control->property = *spec.property;

        const auto property = *spec.property;
        const auto value = (double) effect.getProperty (property, spec.defaultVar());

        if (spec.control == ParamControl::knob)
        {
            control->knob = std::make_unique<DewKnob> (spec.caption, spec.minimum, spec.maximum,
                                                       spec.interval);
            control->knob->setNumDecimalPlaces (spec.decimals);
            control->knob->setBipolar (spec.bipolar);
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
            control->field->setCaption (spec.caption);
            control->field->setSuffix (spec.suffix);
            control->field->setLogarithmic (spec.curve == ParamCurve::logarithmic);
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
