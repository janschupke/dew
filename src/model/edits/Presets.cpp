// =============================================================================
// Loading a preset onto an effect or an instrument.
//
// One of seven translation units behind model/ProjectEdits.h. The header is
// one struct of static functions and stays where it was; this directory is
// where they are defined.
//
// Already fenced off in its own namespace block before this split, which
// is the shape of a file waiting to happen. A preset carries the SOUND
// and nothing else - not a name, a colour, a routing or a level.
// =============================================================================

#include "model/ProjectEdits.h"

#include "model/Ids.h"
#include "model/ModuleState.h"
#include "model/ProjectSchema.h"

namespace dew
{

namespace
{

/** Writes one validated object onto one node, joining the open transaction.

    Every write after the first must join rather than open, or a preset would be
    a hundred undo steps. The transaction is opened by the caller and NOT by
    passing continuingTransaction=false for the first parameter: setProperty
    returns early when the value is already what it should be, before it opens
    anything, so a preset whose first parameter already matched would fold
    silently into whatever step was open.
*/
void writeParams (juce::ValueTree node, const juce::var& values, const ParamSpec* params,
                  int numParams, juce::UndoManager* undo, const juce::String& transactionName)
{
    auto* object = values.getDynamicObject();

    if (object == nullptr || ! node.isValid())
        return;

    for (int i = 0; i < numParams; ++i)
        ProjectEdits::setProperty (node, *params[i].property,
                                   object->getProperty (*params[i].property), undo, transactionName,
                                   /*continuingTransaction*/ true);
}

} // namespace

bool ProjectEdits::applyEffectPreset (juce::ValueTree effect, const Preset& preset,
                                      juce::UndoManager* undo, bool continuingTransaction)
{
    if (! effect.isValid() || ! effect.hasType (ids::EFFECT) || ! preset.isEffect())
        return false;

    const auto slotType = effectTypeFor (effect[ids::type].toString());
    const auto presetType = effectTypeFor (preset.typeId);

    if (! slotType.has_value() || ! presetType.has_value() || *slotType != *presetType)
        return false;

    juce::StringArray warnings;
    const auto& descriptor = effectDescriptor (*slotType);
    const auto values = validateState (descriptor, preset.state, warnings);

    const auto transactionName = "Load preset \"" + preset.name + "\"";

    if (undo != nullptr && ! continuingTransaction)
        undo->beginNewTransaction (transactionName);

    const auto params = effectParamsFor (*slotType);
    writeParams (effect, values, params.data(), (int) params.size(), undo, transactionName);

    return true;
}

bool ProjectEdits::applyInstrumentPreset (juce::ValueTree channel, const Preset& preset,
                                          juce::UndoManager* undo, bool continuingTransaction)
{
    if (! channel.isValid() || ! channel.hasType (ids::CHANNEL) || ! preset.isInstrument())
        return false;

    const auto channelType = instrumentTypeFor (channel[ids::source].toString());
    const auto presetType = instrumentTypeFor (preset.typeId);

    if (! channelType.has_value() || ! presetType.has_value() || *channelType != *presetType)
        return false;

    juce::StringArray warnings;
    const auto& descriptor = instrumentDescriptor (*channelType);
    const auto values = validateState (descriptor, preset.state, warnings);

    auto* object = values.getDynamicObject();

    if (object == nullptr)
        return false;

    const auto transactionName = "Load preset \"" + preset.name + "\"";

    if (undo != nullptr && ! continuingTransaction)
        undo->beginNewTransaction (transactionName);

    for (int g = 0; g < descriptor.numGroups; ++g)
    {
        const auto& group = descriptor.groups[g];

        if (! group.inPreset)
            continue;

        // The SAME walk the capture side uses. It was written out again here,
        // and the two had already drifted once - this one learned to ask which
        // node a group lives on and nodesFor still named ids::SAMPLE - so a
        // generator's group, which hangs off each slot, would have been a third
        // copy to keep in step.
        const auto nodes = nodesFor (channel, group);

        // A nested group's values live INSIDE its owner's element, the way they
        // do in the project file - see stateFor.
        if (group.under != nullptr)
        {
            const auto* owner = groupOn (descriptor, *group.under);

            if (owner == nullptr)
                continue;

            const auto* ownerSlots = object->getProperty (juce::Identifier (owner->jsonKey))
                                         .getArray();

            if (ownerSlots == nullptr)
                continue;

            for (size_t i = 0; i < nodes.size() && (int) i < ownerSlots->size(); ++i)
                if (const auto* slot = (*ownerSlots)[(int) i].getDynamicObject())
                    writeParams (nodes[i], slot->getProperty (juce::Identifier (group.jsonKey)),
                                 group.params, group.numParams, undo, transactionName);

            continue;
        }

        const auto value = object->getProperty (juce::Identifier (group.jsonKey));

        if (group.count <= 1)
        {
            writeParams (nodes.empty() ? juce::ValueTree() : nodes.front(), value, group.params,
                         group.numParams, undo, transactionName);
            continue;
        }

        const auto* slots = value.getArray();

        if (slots == nullptr)
            continue;

        for (size_t i = 0; i < nodes.size() && (int) i < slots->size(); ++i)
            writeParams (nodes[i], (*slots)[(int) i], group.params, group.numParams, undo,
                         transactionName);
    }

    return true;
}

} // namespace dew
