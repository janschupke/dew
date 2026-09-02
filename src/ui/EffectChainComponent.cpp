#include "EffectChainComponent.h"

#include "../engine/Effects.h"
#include "../model/Ids.h"
#include "../model/ProjectSchema.h"
#include "../model/ProjectEdits.h"
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

/** One parameter's caption, range, step and unit.

    A table rather than a switch full of setter calls: adding an effect type is
    a row here, and the layout code never has to know what a "cutoff" is.
*/
struct ParamSpec
{
    const juce::Identifier& property;
    const char* caption;
    double minimum, maximum, interval;
    int decimals;
    const char* suffix;
};

std::vector<ParamSpec> parametersFor (EffectType type)
{
    switch (type)
    {
        case EffectType::filter:
            return { { ids::cutoff,     "CUTOFF", 20.0, 18000.0, 1.0, 0, " Hz" },
                     { ids::resonance,  "RES",    0.05, 4.0,   0.01, 2, "" },
                     { ids::mix,        "MIX",    0.0,  1.0,   0.01, 2, "" } };

        case EffectType::reverb:
            return { { ids::roomSize,   "SIZE",   0.0,  1.0,   0.01, 2, "" },
                     { ids::damping,    "DAMP",   0.0,  1.0,   0.01, 2, "" },
                     { ids::width,      "WIDTH",  0.0,  1.0,   0.01, 2, "" },
                     { ids::mix,        "MIX",    0.0,  1.0,   0.01, 2, "" } };

        case EffectType::delay:
            return { { ids::delayMs,    "TIME",   1.0,  1000.0, 1.0, 0, " ms" },
                     { ids::feedback,   "FBK",    0.0,  0.95,  0.01, 2, "" },
                     { ids::mix,        "MIX",    0.0,  1.0,   0.01, 2, "" } };

        case EffectType::drive:
            return { { ids::drive,      "DRIVE",  1.0,  40.0,  0.1,  1, "" },
                     { ids::outputGain, "OUT",    0.0,  4.0,   0.01, 2, "" },
                     { ids::mix,        "MIX",    0.0,  1.0,   0.01, 2, "" } };

        case EffectType::chorus:
            return { { ids::rate,       "RATE",   0.01, 20.0,  0.01, 2, " Hz" },
                     { ids::depth,      "DEPTH",  0.0,  1.0,   0.01, 2, "" },
                     { ids::mix,        "MIX",    0.0,  1.0,   0.01, 2, "" } };

        case EffectType::eq:
            return { { ids::lowGainDb,  "LOW",    -24.0, 24.0, 0.1,  1, " dB" },
                     { ids::midGainDb,  "MID",    -24.0, 24.0, 0.1,  1, " dB" },
                     { ids::midFreq,    "FREQ",   100.0, 8000.0, 1.0, 0, " Hz" },
                     { ids::highGainDb, "HIGH",   -24.0, 24.0, 0.1,  1, " dB" } };
    }

    return {};
}

} // namespace

/** One slot in the list: bypass, icon, name, reorder, remove. */
class EffectChainComponent::SlotRow : public juce::Component
{
public:
    SlotRow (EffectChainComponent& o, ProjectDocument& d, juce::ValueTree e, int i)
        : owner (o), document (d), effect (std::move (e)), index (i)
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

        upButton.onClick = [this] { owner.selectSlot (index); move (-1); };
        addAndMakeVisible (upButton);

        downButton.onClick = [this] { owner.selectSlot (index); move (1); };
        addAndMakeVisible (downButton);

        removeButton.onClick = [this]
        {
            owner.selectSlot (index);
            auto& undo = document.getUndoManager();
            undo.beginNewTransaction ("Remove effect");
            ProjectEdits::removeEffect (owner.getOwner(), effect, &undo);
        };
        addAndMakeVisible (removeButton);

        forwardChildMouseEventsTo (*this);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void setSelected (bool shouldBeSelected)
    {
        if (std::exchange (selected, shouldBeSelected) != shouldBeSelected)
            repaint();
    }

    void mouseDown (const juce::MouseEvent&) override { owner.selectSlot (index); }

    void mouseEnter (const juce::MouseEvent&) override { setHovered (true); }
    void mouseExit (const juce::MouseEvent&) override  { setHovered (isMouseOver (true)); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (selected ? colour::surfaceHover
                            : hovered ? colour::surfaceRaised.brighter (0.06f)
                                      : colour::surfaceRaised);

        if (selected)
        {
            g.setColour (colour::accent);
            g.fillRect (0, 0, 3, getHeight());
        }

        const auto bypassed = ! (bool) effect[ids::enabled];
        const auto textColour = bypassed ? colour::textDisabled : colour::textPrimary;

        icons::draw (g, iconForType (type), iconBounds, textColour);

        g.setColour (textColour);
        g.setFont (type::font (type::small));
        g.drawText (effectTypeDisplayName (type), nameBounds,
                    juce::Justification::centredLeft, false);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (space::xs, space::xxs);
        area.removeFromLeft (space::xxs);

        removeButton.setBounds (area.removeFromRight (20));
        downButton.setBounds (area.removeFromRight (18));
        upButton.setBounds (area.removeFromRight (18));
        area.removeFromRight (space::xs);

        bypassButton.setBounds (area.removeFromLeft (20));
        area.removeFromLeft (space::xs);
        iconBounds = area.removeFromLeft (16).toFloat();
        area.removeFromLeft (space::xs);
        nameBounds = area;
    }

private:
    void move (int delta)
    {
        auto& undo = document.getUndoManager();
        undo.beginNewTransaction ("Reorder effects");
        ProjectEdits::moveEffect (owner.getOwner(), effect, index + delta, &undo);
    }

    void setHovered (bool shouldBeHovered)
    {
        if (std::exchange (hovered, shouldBeHovered) != shouldBeHovered)
            repaint();
    }

    EffectChainComponent& owner;
    ProjectDocument& document;
    juce::ValueTree effect;
    int index = 0;
    EffectType type = EffectType::filter;
    bool selected = false;
    bool hovered = false;

    juce::Rectangle<float> iconBounds;
    juce::Rectangle<int> nameBounds;

    DewIconButton bypassButton { icons::power(), "Bypass this effect" };
    DewIconButton upButton { icons::chevronUp(), "Move earlier in the chain" };
    DewIconButton downButton { icons::chevronDown(), "Move later in the chain" };
    DewIconButton removeButton { icons::trash(), "Remove this effect" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SlotRow)
};

// -----------------------------------------------------------------------------

EffectChainComponent::EffectChainComponent (ProjectDocument& d)
    : document (d)
{
    setComponentID ("effectChain");

    addButton.onClick = [this] { showAddMenu(); };
    addAndMakeVisible (addButton);

    document.getState().addListener (this);
}

EffectChainComponent::~EffectChainComponent()
{
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

    if (wanted == selectedSlot && ! params.isEmpty())
        return;

    selectedSlot = wanted;

    for (int i = 0; i < slotRows.size(); ++i)
        slotRows[i]->setSelected (i == selectedSlot);

    rebuildParameters();
    resized();
}

void EffectChainComponent::addEffectOfType (const juce::String& type)
{
    if (! chainOwner.isValid())
        return;

    auto& undo = document.getUndoManager();
    undo.beginNewTransaction ("Add effect");

    if (ProjectEdits::addEffect (document.getState(), chainOwner, type, &undo).isValid())
        selectedSlot = ProjectEdits::countEffects (chainOwner) - 1;
}

void EffectChainComponent::showAddMenu()
{
    if (ProjectEdits::countEffects (chainOwner) >= kMaxEffectsPerChain)
        return;

    juce::PopupMenu menu;

    const EffectType types[] { EffectType::filter, EffectType::reverb, EffectType::delay,
                               EffectType::drive, EffectType::chorus, EffectType::eq };

    for (int i = 0; i < (int) std::size (types); ++i)
        menu.addItem (i + 1, effectTypeDisplayName (types[i]));

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addButton),
                        [this] (int choice)
                        {
                            if (choice <= 0)
                                return;

                            const EffectType chosen[] { EffectType::filter, EffectType::reverb,
                                                        EffectType::delay, EffectType::drive,
                                                        EffectType::chorus, EffectType::eq };
                            addEffectOfType (effectTypeToString (chosen[choice - 1]));
                        });
}

void EffectChainComponent::rebuild()
{
    const juce::ScopedValueSetter<bool> quiet (rebuilding, true);

    slotRows.clear();

    int index = 0;

    for (const auto& child : chainOwner)
        if (child.hasType (ids::EFFECT))
            addAndMakeVisible (slotRows.add (new SlotRow (*this, document, child, index++)));

    selectedSlot = juce::jlimit (0, juce::jmax (0, slotRows.size() - 1), selectedSlot);

    for (int i = 0; i < slotRows.size(); ++i)
        slotRows[i]->setSelected (i == selectedSlot);

    addButton.setEnabled (chainOwner.isValid() && slotRows.size() < kMaxEffectsPerChain);

    rebuildParameters();
    resized();
    repaint();
}

void EffectChainComponent::rebuildParameters()
{
    params.clear();
    modeBox.reset();

    const auto effect = effectAt (selectedSlot);

    if (! effect.isValid())
        return;

    const auto type = effectTypeFromString (effect[ids::type].toString());

    // The filter is the one type with a discrete choice as well as numbers.
    if (type == EffectType::filter)
    {
        modeBox = std::make_unique<juce::ComboBox>();
        modeBox->addItem ("Low pass", 1);
        modeBox->addItem ("High pass", 2);
        modeBox->addItem ("Band pass", 3);

        const auto mode = filterModeFromString (effect[ids::filterMode].toString());
        modeBox->setSelectedId (mode == FilterMode::lowpass ? 1 : mode == FilterMode::highpass ? 2 : 3,
                                juce::dontSendNotification);

        modeBox->onChange = [this, effect]
        {
            if (rebuilding)
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
        control->field = std::make_unique<DewNumberField>();

        auto* field = control->field.get();
        field->setRange (spec.minimum, spec.maximum, spec.interval);
        field->setNumDecimalPlaces (spec.decimals);
        field->setCaption (spec.caption);
        field->setSuffix (spec.suffix);
        field->setValue ((double) effect[spec.property], juce::dontSendNotification);

        const auto property = spec.property;

        field->onEditStart = [this] { document.getUndoManager().beginNewTransaction ("Change effect"); };
        field->onValueChange = [this, field, effect, property]
        {
            if (rebuilding)
                return;

            auto mutableEffect = effect;
            mutableEffect.setProperty (property, field->getValue(), &document.getUndoManager());
        };

        addAndMakeVisible (*field);
        params.add (control.release());
    }
}

int EffectChainComponent::getRequiredHeight() const
{
    const auto rows = juce::jmax (1, slotRows.size());
    const auto paramRows = (params.size() + 2) / 3;      // three across
    const auto modeRows = modeBox != nullptr ? 1 : 0;

    return headerHeight + rows * rowHeight + space::sm
           + (paramRows + modeRows) * paramRowHeight + space::sm;
}

void EffectChainComponent::resized()
{
    auto area = getLocalBounds();

    auto header = area.removeFromTop (headerHeight);
    addButton.setBounds (header.removeFromRight (headerHeight).reduced (space::xxs));

    for (auto* row : slotRows)
        row->setBounds (area.removeFromTop (rowHeight));

    area.removeFromTop (space::sm);

    if (modeBox != nullptr)
    {
        modeBox->setBounds (area.removeFromTop (paramRowHeight).reduced (space::xxs, space::sm));
        area.removeFromTop (space::xxs);
    }

    // Three parameters across, so a four-parameter EQ wraps rather than
    // squeezing every field below the width a drag needs to be usable.
    const auto columns = 3;

    for (int i = 0; i < params.size(); i += columns)
    {
        auto row = area.removeFromTop (paramRowHeight);
        const auto width = juce::jmax (1, row.getWidth() / columns);

        for (int c = 0; c < columns && i + c < params.size(); ++c)
            params[i + c]->field->setBounds (row.removeFromLeft (width).reduced (space::xxs));
    }
}

void EffectChainComponent::paint (juce::Graphics& g)
{
    paint::caption (g, { 0, 0, getWidth() - headerHeight, headerHeight }, "EFFECTS");

    if (! chainOwner.isValid())
    {
        g.setColour (colour::textDisabled);
        g.setFont (type::font (type::small));
        g.drawText ("Nothing selected", getLocalBounds().withTrimmedTop (headerHeight),
                    juce::Justification::centredTop, false);
        return;
    }

    if (slotRows.isEmpty())
    {
        g.setColour (colour::textDisabled);
        g.setFont (type::font (type::small));
        g.drawText ("No effects - use + to add one",
                    juce::Rectangle<int> (space::sm, headerHeight, getWidth() - space::sm, rowHeight),
                    juce::Justification::centredLeft, false);
    }
}

// A chain edit anywhere in the document can be this owner's, and the rows carry
// their index, so anything structural rebuilds rather than being patched.
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

void EffectChainComponent::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    if (! tree.hasType (ids::EFFECT))
        return;

    // The type is not editable in place, but enabled is, and a bypassed effect
    // has to look bypassed.
    if (property == ids::enabled)
        repaint();

    if (tree == effectAt (selectedSlot))
    {
        const juce::ScopedValueSetter<bool> quiet (rebuilding, true);

        for (auto* control : params)
            if (control->property == property)
                control->field->setValue ((double) tree[property], juce::dontSendNotification);
    }
}

} // namespace dew
