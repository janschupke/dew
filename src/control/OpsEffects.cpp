#include "control/OpsSupport.h"
#include "model/ModuleCatalog.h"
#include "model/PresetCategory.h"
#include "model/PresetLibrary.h"
#include "model/ProjectSchema.h"

namespace dew::control
{

namespace
{

/** One effect, addressed: whose chain, and which slot of it. */
std::vector<ArgSpec> slotFields()
{
    return { { "target", ValueKind::text, true, "channel, mixerTrack or master." },
             { "id", ValueKind::integer, false,
               "The channel id or mixer track id. Unread by master." },
             { "slot", ValueKind::integer, true, "Which effect in that chain, counting from 0." } };
}

/** The position of an effect among its chain's EFFECT siblings.

    Counted rather than remembered, because an entry that ADDED the effect does
    not know where it landed and an entry that changed one already knew - and
    reporting "the last slot" for both was wrong for every update.
*/
int slotOfEffect (const juce::ValueTree& owner, const juce::ValueTree& effect)
{
    auto index = 0;

    for (const auto& child : owner)
    {
        if (! child.hasType (ids::EFFECT))
            continue;

        if (child == effect)
            return index;

        ++index;
    }

    return -1;
}

/** The effect types, from the catalog rather than a list.

    effectDescriptors() is the table that joins a type to what a .dew stores for
    it, so a seventh effect becomes accepted here, and named in the error when
    it is not, without an edit.
*/
juce::StringArray effectTypes()
{
    juce::StringArray types;

    for (const auto& descriptor : effectDescriptors())
        types.add (descriptor.id);

    return types;
}

juce::ValueTree effectAt (const juce::ValueTree& owner, int slot)
{
    auto index = 0;

    for (const auto& child : owner)
        if (child.hasType (ids::EFFECT) && index++ == slot)
            return child;

    return {};
}

ControlResult write (ControlHost& host, const juce::var& args)
{
    auto project = host.project();

    if (! project.isValid())
        return ControlResult::failure ("no project is open.");

    const auto& entries = arrayArg (args, "entries");
    const auto types = effectTypes();

    for (int i = 0; i < entries.size(); ++i)
    {
        const auto& entry = entries.getReference (i);
        const auto at = "entries[" + juce::String (i) + "] ";
        const auto owner = chainOwnerFor (project, textArg (entry, "target"), intArg (entry, "id"));

        if (! owner.isValid())
            return ControlResult::failure (at + "addresses no effect chain.");

        if (hasArg (entry, "slot"))
        {
            if (! effectAt (owner, intArg (entry, "slot")).isValid())
                return ControlResult::failure (at + "has no effect in slot "
                                               + juce::String (intArg (entry, "slot")) + ".");
        }
        else if (! types.contains (textArg (entry, "type")))
        {
            return ControlResult::failure (at + "'" + textArg (entry, "type")
                                           + "' is not an effect. Use "
                                           + types.joinIntoString (", ") + ".");
        }
        else if (ProjectEdits::countEffects (owner) >= kMaxEffectsPerChain)
        {
            return ControlResult::failure (at + "that chain is full: it already holds "
                                           + juce::String (kMaxEffectsPerChain)
                                           + " effects, which is as many as the engine renders.");
        }
    }

    auto* undo = host.undoManager();

    juce::Array<juce::var> touched;

    for (const auto& entry : entries)
    {
        const auto owner = chainOwnerFor (project, textArg (entry, "target"), intArg (entry, "id"));

        auto effect = hasArg (entry, "slot")
                          ? effectAt (owner, intArg (entry, "slot"))
                          : ProjectEdits::addEffect (project, owner, textArg (entry, "type"), undo);

        if (! effect.isValid())
            continue;

        // Named by the property it writes, which is the rule for every wire
        // argument backed by one - the same shape channels_write's `muted`
        // already has. It was a literal beside ids::enabled on the very next
        // line, which is two spellings of one fact.
        if (hasArg (entry, ids::enabled))
            ProjectEdits::setProperty (effect, ids::enabled, flagArg (entry, ids::enabled), undo,
                                       "Turn effect off", true);

        if (hasArg (entry, "preset"))
        {
            const auto wanted = textArg (entry, "preset");

            for (const auto& preset : PresetLibrary::all())
                if (preset.isEffect() && preset.name == wanted)
                    ProjectEdits::applyEffectPreset (effect, preset, undo, true);
        }

        touched.add (Obj {}
                         .set ("target", entry[juce::Identifier ("target")])
                         .set ("id", intArg (entry, "id"))
                         .set ("slot", slotOfEffect (owner, effect))
                         .set ("type", effect[ids::type].toString()));
    }

    host.flushEngine();

    return ControlResult::success (
        Obj {}.set ("applied", touched.size()).set ("effects", arrayOf (touched)));
}

ControlResult move (ControlHost& host, const juce::var& args)
{
    auto project = host.project();
    const auto owner = chainOwnerFor (project, textArg (args, "target"), intArg (args, "id"));

    if (! owner.isValid())
        return ControlResult::failure ("that addresses no effect chain.");

    const auto effect = effectAt (owner, intArg (args, "slot"));

    if (! effect.isValid())
        return ControlResult::failure ("there is no effect in slot "
                                       + juce::String (intArg (args, "slot")) + ".");

    // Position is counted among EFFECTS only, so the instrument child a channel
    // also carries cannot shift the result.
    ProjectEdits::moveEffect (owner, effect, intArg (args, "toSlot"), host.undoManager());
    host.flushEngine();

    return ControlResult::success (Obj {}.set ("done", true));
}

ControlResult remove (ControlHost& host, const juce::var& args)
{
    auto project = host.project();

    if (! project.isValid())
        return ControlResult::failure ("no project is open.");

    // Slots are resolved to NODES before anything is removed. Removing by index
    // in a loop renumbers the slots the later entries name, so a batch removing
    // slots 0 and 1 would remove 0 and then whatever slid into 1.
    struct Doomed
    {
        juce::ValueTree owner;
        juce::ValueTree effect;
    };

    std::vector<Doomed> doomed;

    for (const auto& entry : arrayArg (args, "entries"))
    {
        const auto owner = chainOwnerFor (project, textArg (entry, "target"), intArg (entry, "id"));
        const auto effect = effectAt (owner, intArg (entry, "slot"));

        if (! effect.isValid())
            return ControlResult::failure (
                "there is no effect in slot " + juce::String (intArg (entry, "slot")) + " of "
                + textArg (entry, "target") + " " + juce::String (intArg (entry, "id")) + ".");

        doomed.push_back ({ owner, effect });
    }

    for (const auto& entry : doomed)
        ProjectEdits::removeEffect (entry.owner, entry.effect, host.undoManager());

    host.flushEngine();

    return applied ((int) doomed.size());
}

ControlResult listPresets (ControlHost&, const juce::var& args)
{
    const auto wantedKind = textArg (args, "kind");
    const auto wantedType = textArg (args, "type");

    juce::Array<juce::var> rows;

    for (const auto& preset : PresetLibrary::all())
    {
        if (wantedKind.isNotEmpty() && preset.kind != wantedKind)
            continue;

        if (wantedType.isNotEmpty() && preset.typeId != wantedType)
            continue;

        rows.add (Obj {}
                      .set ("name", preset.name)
                      .set ("kind", preset.kind)
                      .set ("type", preset.typeId)
                      .set ("description", preset.description)
                      .set ("category", preset.category.has_value()
                                            ? presetCategoryToString (*preset.category)
                                            : juce::String {}));
    }

    return ControlResult::success (Obj {}.set ("presets", arrayOf (rows)));
}

} // namespace

void appendEffectOps (std::vector<OpSpec>& all)
{
    all.push_back (
        { "effects_write",
          OpScope::write,
          OpEdits::yes,
          "Add effects to a channel, mixer insert or the master, or bypass and preset existing "
          "ones.",
          "An entry with a `slot` changes the effect already there; an entry without one "
          "appends an effect of `type` to the end of the chain. A chain holds four, and "
          "a fifth is refused rather than silently dropped - an effect the engine does "
          "not render is an effect that appears to have stopped working.\n\n"
          "The effect's own parameters are params_write's, addressed with group "
          "'effects' and the same slot. `preset` here is a shorthand for the whole set "
          "of them at once; presets_list says what is available.",
          { { "entries",
              ValueKind::array,
              true,
              "The effects to add or change.",
              { { "target", ValueKind::text, true, "channel, mixerTrack or master." },
                { "id", ValueKind::integer, false, "The channel or mixer track id." },
                { "slot", ValueKind::integer, false,
                  "An existing slot to change. Omit to append a new effect." },
                { "type", ValueKind::text, false, "What effect to add. Read only when adding." },
                { ids::enabled.toString(), ValueKind::flag, false, "False bypasses the slot." },
                { "preset", ValueKind::text, false,
                  "A factory preset's name, applied to this slot." } } } },
          write });

    all.push_back ({ "effects_move",
                     OpScope::write,
                     OpEdits::yes,
                     "Move one effect to another position in its chain.",
                     "Order is audible: a distortion before a reverb is not a distortion after "
                     "one. Positions count effects only, so the instrument a channel also carries "
                     "cannot shift the result.\n\n"
                     "An effect keeps its own DSP unit across the move - the pool is keyed on the "
                     "effect's identity rather than its position - so reordering a chain does not "
                     "cut the reverb tail the slots after it were in the middle of.",
                     { { "target", ValueKind::text, true, "channel, mixerTrack or master." },
                       { "id", ValueKind::integer, false, "The channel or mixer track id." },
                       { "slot", ValueKind::integer, true, "Which effect to move." },
                       { "toSlot", ValueKind::integer, true, "Where it should end up." } },
                     move });

    all.push_back (
        { "effects_remove",
          OpScope::write,
          OpEdits::yes,
          "Remove effects from chains, as one undo step.",
          "Slots are resolved before anything is removed, so removing slots 0 and 1 in "
          "one call removes the two you meant rather than the first and whatever slid "
          "up into its place.",
          { { "entries", ValueKind::array, true, "The effects to remove.", slotFields() } },
          remove });

    all.push_back (
        { "presets_list",
          OpScope::read,
          OpEdits::no,
          "List the factory presets, for instruments and for effects.",
          "Filter by `kind` ('instrument' or 'effect') and by `type` - the id of an "
          "effect or an instrument, as project_describe reports a channel's source. A "
          "preset is refused if its type is not the slot's, so filtering first is worth "
          "doing.",
          { { "kind", ValueKind::text, false, "instrument or effect." },
            { "type", ValueKind::text, false, "An effect or instrument id, such as reverb." } },
          listPresets });
}

} // namespace dew::control
