// =============================================================================
// Effect chains, and the oscillator slots beside them.
//
// One of seven translation units behind model/ProjectEdits.h. The header is
// one struct of static functions and stays where it was; this directory is
// where they are defined.
//
// An effect chain hangs off a channel OR a mixer track OR the master, and
// nothing here asks which: they carry the same EFFECT children, which is
// what lets one editor drive all three.
// =============================================================================

#include "model/ProjectEdits.h"

#include "model/Ids.h"
#include "model/ProjectSchema.h"

namespace dew
{

int ProjectEdits::countEffects (const juce::ValueTree& owner)
{
    int count = 0;

    for (const auto& child : owner)
        if (child.hasType (ids::EFFECT))
            ++count;

    return count;
}

juce::Array<juce::ValueTree> ProjectEdits::effectChainOwners (const juce::ValueTree& project)
{
    juce::Array<juce::ValueTree> owners;

    for (const auto& child : project)
        if (child.hasType (ids::CHANNEL))
            owners.add (child);

    const auto mixer = project.getChildWithName (ids::MIXER);

    for (const auto& track : mixer)
        if (track.hasType (ids::MIXER_TRACK))
            owners.add (track);

    // The master carries a chain like any other bus, so it has to be counted
    // among the owners a new id is derived from. Left out, two effects added to
    // the master both took `highest + 1` and got the SAME id - and the engine
    // keys DSP state on the id, so they shared one module and fought over the
    // same reverb tank, which is exactly what unique ids exist to prevent.
    if (const auto master = mixer.getChildWithName (ids::MASTER); master.isValid())
        owners.add (master);

    return owners;
}

juce::ValueTree ProjectEdits::oscillatorAt (const juce::ValueTree& channel, int index)
{
    if (index < 0)
        return {};

    int seen = 0;

    for (const auto& node : channel.getChildWithName (ids::INSTRUMENT))
        if (node.hasType (ids::OSC) && seen++ == index)
            return node;

    return {};
}

int ProjectEdits::countOscillators (const juce::ValueTree& channel)
{
    int count = 0;

    for (const auto& node : channel.getChildWithName (ids::INSTRUMENT))
        if (node.hasType (ids::OSC))
            ++count;

    return count;
}

juce::ValueTree ProjectEdits::addEffect (juce::ValueTree project, juce::ValueTree owner,
                                         const juce::String& type, juce::UndoManager* undo)
{
    if (! owner.isValid() || countEffects (owner) >= kMaxEffectsPerChain)
        return {};

    auto effect = defaultTreeFor (
        childSpecFor (childSpecFor (projectSpec(), "channels"), "effects"));
    effect.setProperty (ids::type, type, nullptr);

    // Effect ids are unique across the whole project, not per chain: the engine
    // keys each effect's DSP state on its id, so two effects sharing one would
    // fight over the same reverb tank.
    int highest = 0;

    for (const auto& chainOwner : effectChainOwners (project))
        for (const auto& existing : chainOwner)
            if (existing.hasType (ids::EFFECT))
                highest = juce::jmax (highest, (int) existing[ids::id]);

    effect.setProperty (ids::id, highest + 1, nullptr);

    owner.appendChild (effect, undo);
    return effect;
}

void ProjectEdits::removeEffect (juce::ValueTree owner, juce::ValueTree effect,
                                 juce::UndoManager* undo)
{
    const auto index = owner.indexOf (effect);

    if (index >= 0)
        owner.removeChild (index, undo);
}

void ProjectEdits::moveEffect (juce::ValueTree owner, juce::ValueTree effect, int newPosition,
                               juce::UndoManager* undo)
{
    const auto count = countEffects (owner);

    if (count <= 1)
        return;

    const auto target = juce::jlimit (0, count - 1, newPosition);

    // Positions are counted among effects, but ValueTree indices count every
    // child - a channel also holds its instrument - so translate.
    int seen = 0;
    int targetIndex = -1;

    for (int i = 0; i < owner.getNumChildren(); ++i)
        if (owner.getChild (i).hasType (ids::EFFECT) && seen++ == target)
            targetIndex = i;

    const auto from = owner.indexOf (effect);

    if (from >= 0 && targetIndex >= 0 && from != targetIndex)
        owner.moveChild (from, targetIndex, undo);
}

} // namespace dew
