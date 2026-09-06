#include "control/OpsSupport.h"
#include "control/ParamAddress.h"
#include "model/ModuleCatalog.h"

namespace dew::control
{

namespace
{

/** The address arguments, declared once.

    Eight operations take an address and every one of them takes the same five
    fields with the same meanings. Written out once here so a caller reading two
    tool schemas is reading the same words, and so a change to what an address
    is happens in one place.
*/
std::vector<ArgSpec> addressFields()
{
    return { { "target", ValueKind::text, true, "project, channel, mixerTrack or master." },
             { "id", ValueKind::integer, false,
               "The channel id or mixer track id. Unread by project and master." },
             { "group", ValueKind::text, false,
               "oscillators, amp, sample, soundfont or effects. Omit for the target's own "
               "parameters." },
             { "slot", ValueKind::integer, false,
               "Which oscillator or which effect slot, counting from 0." },
             { "param", ValueKind::text, true, "The parameter's name, as params_list gives it." } };
}

/** A parameter's value, in the type the file stores it as.

    A parameter is a number, a choice spelled as an id, or a boolean, and which
    of the three is the spec's business - defaultVar draws exactly this
    distinction and is the reason the shape is stated here rather than guessed
    from what arrived.

    Returns the fault, or an empty string. A choice given a name it does not
    have is told which names it does have: an unrecognised mode written through
    as a string would sit in the file being ignored by the engine, which is the
    silent wrongness effectTypeFor was written to stop.
*/
juce::String coerceToSpec (const ParamSpec& spec, const juce::var& given, juce::var& out)
{
    if (spec.choices != nullptr && spec.numChoices > 0)
    {
        const auto text = given.toString();
        juce::StringArray offered;

        for (int i = 0; i < spec.numChoices; ++i)
        {
            offered.add (spec.choices[i].id);

            if (text == spec.choices[i].id)
            {
                out = juce::var (text);
                return {};
            }
        }

        return "'" + text + "' is not one of " + offered.joinIntoString (", ") + ".";
    }

    if (spec.control == ParamControl::toggle)
    {
        out = juce::var (given.isBool() ? (bool) given : (double) given != 0.0);
        return {};
    }

    if (! (given.isDouble() || given.isInt() || given.isInt64() || given.isBool()))
        return "'" + given.toString() + "' is not a number.";

    // Clamped rather than refused. The range is the spec's and the caller's
    // arithmetic is its own; a filter asked for 25kHz wants the top of the
    // range, and refusing the whole batch over it would be a worse answer than
    // giving it the loudest thing it can have. The answer reports it.
    const auto clamped = spec.clamp ((double) given);

    out = spec.integral ? juce::var ((int) clamped) : juce::var (clamped);

    return {};
}

juce::var describeSpec (const ParamSpec& spec, const juce::var& current)
{
    Obj row;
    row.set ("param", spec.property->toString())
        .set ("current", current)
        .set ("default", spec.defaultVar())
        .set ("automatable", spec.automatable);

    if (spec.choices != nullptr && spec.numChoices > 0)
    {
        juce::Array<juce::var> choices;

        for (int i = 0; i < spec.numChoices; ++i)
            choices.add (juce::var (spec.choices[i].id));

        return row.set ("kind", "choice").set ("choices", arrayOf (choices));
    }

    if (spec.control == ParamControl::toggle)
        return row.set ("kind", "flag");

    return row.set ("kind", spec.integral ? "integer" : "number")
        .set ("minimum", spec.minimum)
        .set ("maximum", spec.maximum)
        .set ("suffix", spec.suffix)
        .set ("bipolar", spec.bipolar);
}

/** Every parameter at an address, with what it is and what it holds now. */
ControlResult list (ControlHost& host, const juce::var& args)
{
    const auto project = host.project();

    if (! project.isValid())
        return ControlResult::failure ("no project is open.");

    auto address = addressFrom (args);
    address.param = juce::Identifier ("unnamed");

    const auto node = paramNodeFor (project, address);

    if (! node.isValid())
        return ControlResult::failure ("nothing is addressed by " + address.describe() + ".");

    juce::Array<juce::var> rows;

    for (const auto& spec : paramsAt (project, address))
        rows.add (describeSpec (spec, paramValueNode (node, *spec.property)[*spec.property]));

    return ControlResult::success (
        Obj {}.set ("node", node.getType().toString()).set ("parameters", arrayOf (rows)));
}

ControlResult read (ControlHost& host, const juce::var& args)
{
    const auto project = host.project();

    if (! project.isValid())
        return ControlResult::failure ("no project is open.");

    juce::Array<juce::var> values;

    for (const auto& entry : arrayArg (args, "entries"))
    {
        const auto address = addressFrom (entry);
        const auto node = paramValueNodeFor (project, address);
        const auto spec = paramSpecFor (project, address);

        if (! node.isValid() || ! spec.has_value())
        {
            values.add (Obj {}.set ("address", address.describe()).set ("found", false));
            continue;
        }

        values.add (Obj {}
                        .set ("address", address.describe())
                        .set ("found", true)
                        .set ("value", node[address.param]));
    }

    return ControlResult::success (Obj {}.set ("values", arrayOf (values)));
}

ControlResult write (ControlHost& host, const juce::var& args)
{
    auto project = host.project();

    if (! project.isValid())
        return ControlResult::failure ("no project is open.");

    const auto& entries = arrayArg (args, "entries");

    // Every address is resolved BEFORE anything is written, so a batch with a
    // typo in its last entry changes nothing rather than half of what it said.
    // Half a batch is the worst answer available: the caller is told it failed
    // and the document has moved anyway.
    struct Pending
    {
        juce::ValueTree node;
        juce::Identifier property;
        juce::var value;
    };

    std::vector<Pending> pending;

    for (int i = 0; i < entries.size(); ++i)
    {
        const auto& entry = entries.getReference (i);
        const auto at = "entries[" + juce::String (i) + "] ";
        const auto address = addressFrom (entry);
        const auto node = paramValueNodeFor (project, address);

        if (! node.isValid())
            return ControlResult::failure (at + "addresses nothing: " + address.describe() + ".");

        const auto spec = paramSpecFor (project, address);

        if (! spec.has_value())
            return ControlResult::failure (at + address.param.toString() + " is not a parameter of "
                                           + address.describe()
                                           + ". Call params_list to see what is.");

        juce::var value;
        const auto fault = coerceToSpec (*spec, entry[juce::Identifier ("value")], value);

        if (fault.isNotEmpty())
            return ControlResult::failure (at + address.describe() + ": " + fault);

        pending.push_back ({ node, address.param, value });
    }

    beginOneTransaction (host, "params_write");

    for (const auto& write : pending)
        ProjectEdits::setProperty (write.node, write.property, write.value, host.undoManager(),
                                   "Set parameter", true);

    host.flushEngine();

    return applied ((int) pending.size());
}

std::vector<ArgSpec> entryFields()
{
    auto fields = addressFields();
    fields.push_back ({ "value", ValueKind::any, true,
                        "A number, or the id of a choice, or true/false. params_list says which "
                        "this parameter takes." });

    return fields;
}

} // namespace

void appendParamOps (std::vector<OpSpec>& all)
{
    all.push_back ({ "params_list", OpScope::read,
                     "List every parameter at one address, with its range, its default, whether it "
                     "can be automated, and what it holds now.",
                     "How to find out what is writable before writing it. An address is a target "
                     "(project, channel, mixerTrack, master), an id, a group and a slot - the same "
                     "five fields every parameter operation takes.\n\n"
                     "Leave `group` off for the target's own parameters. Set it to 'oscillators', "
                     "'amp', 'sample' or 'soundfont' for part of a channel's instrument, or to "
                     "'effects' with a slot for one effect. What a channel offers depends on what "
                     "kind of instrument it carries, which project_describe reports as its source.",
                     addressFields(), list });

    all.push_back (
        { "params_read",
          OpScope::read,
          "Read the current value of any number of parameters.",
          "Takes a list of addresses and answers with a value for each. An address "
          "that names nothing comes back with found=false rather than failing the "
          "batch, so one stale id does not cost you the other forty answers.",
          { { "entries", ValueKind::array, true, "The addresses to read.", addressFields() } },
          read });

    all.push_back (
        { "params_write",
          OpScope::write,
          "Set any number of parameters anywhere in the project, as one undo step.",
          "The one tool for every value in the document: a channel's volume, an "
          "oscillator's detune, an envelope's release, a sample's fade, a soundfont's "
          "tuning, any parameter of any effect, a mixer fader, the master, the tempo.\n\n"
          "Every address is resolved before anything is written, so a batch with a bad "
          "entry changes nothing at all rather than half of what it asked for. A value "
          "outside a parameter's range is clamped to it; a choice given a name it does "
          "not have fails and says which names it has.\n\n"
          "The whole call is one undo step, however many entries it carried.",
          { { "entries", ValueKind::array, true, "The parameters to set, and what to set them to.",
              entryFields() } },
          write });
}

} // namespace dew::control
