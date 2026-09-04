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

    const auto instrument = channel.getChildWithName (ids::INSTRUMENT);

    for (int g = 0; g < descriptor.numGroups; ++g)
    {
        const auto& group = descriptor.groups[g];

        if (! group.inPreset)
            continue;

        const auto value = object->getProperty (juce::Identifier (group.jsonKey));
        // Which node a group lives on, not which name it happens to have. It
        // was a compare against ids::SAMPLE alone, and a third instrument whose
        // parameters also hang off the CHANNEL would have had every one of them
        // written into an INSTRUMENT child that does not contain them - a
        // preset that loaded, reported success and changed nothing.
        const auto parent = channel.getChildWithName (*group.node).isValid() ? channel : instrument;

        if (group.count <= 1)
        {
            writeParams (parent.getChildWithName (*group.node), value, group.params,
                         group.numParams, undo, transactionName);
            continue;
        }

        const auto* slots = value.getArray();

        if (slots == nullptr)
            continue;

        auto index = 0;

        for (const auto& child : parent)
        {
            if (! child.hasType (*group.node))
                continue;

            if (index >= slots->size())
                break;

            writeParams (child, (*slots)[index], group.params, group.numParams, undo,
                         transactionName);
            ++index;
        }
    }

    return true;
}

} // namespace dew
