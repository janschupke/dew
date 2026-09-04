#include "control/OpsSupport.h"
#include "model/ModuleCatalog.h"

namespace dew::control
{

namespace
{

/** Adds a channel of the kind its stored `source` names.

    The three adders differ in what instrument they build and in nothing else -
    the id, the mixer routing and the canonical child order are the same problem
    - so the choice is made here once rather than by three near-identical
    operations a caller would have to choose between.
*/
juce::ValueTree addOfKind (juce::ValueTree project, const juce::String& kind,
                           const juce::String& name, juce::UndoManager* undo)
{
    if (kind == "audio")
        return ProjectEdits::addAudioChannel (project, name, undo);

    if (kind == "soundfont")
        return ProjectEdits::addSoundFontChannel (project, name, undo);

    return ProjectEdits::addChannel (project, name, undo);
}

/** The kinds a channel may be, taken from the catalog rather than listed.

    instrumentDescriptors() is the table that says what an instrument type is
    called in a file, so a fourth instrument becomes an accepted kind with no
    edit here and the error message names it without being told.
*/
juce::StringArray channelKinds()
{
    juce::StringArray kinds;

    for (const auto& descriptor : instrumentDescriptors())
        kinds.add (descriptor.id);

    return kinds;
}

/** One entry of channels_write.

    Built by a function rather than written as a brace list inside the
    operation's own, because `muted` is NAMED by the identifier it writes - so
    the shape is not a constant, and the nested aggregate form does not take
    one. The gate on parameters spelled as literals is what asks for this, and
    it is right to: a `"muted"` beside `ids::muted` is two spellings of one
    fact.
*/
std::vector<ArgSpec> channelFields()
{
    return { { "id", ValueKind::integer, false,
               "An existing channel to change. Omit to add a new one." },
             { "kind", ValueKind::text, false,
               "synth, audio or soundfont. Read only when adding." },
             { "name", ValueKind::text, false, "What the channel is called." },
             { "colour", ValueKind::text, false,
               "A hex colour. Empty means inherit the colour of its position." },
             { "mixerTrackId", ValueKind::integer, false, "The mixer insert to route into." },
             { ids::muted.toString(), ValueKind::flag, false, "Whether the channel is silent." } };
}

ControlResult write (ControlHost& host, const juce::var& args)
{
    auto project = host.project();

    if (! project.isValid())
        return ControlResult::failure ("no project is open.");

    const auto& entries = arrayArg (args, "entries");
    const auto kinds = channelKinds();

    // Resolved before written, so a batch with one bad entry changes nothing.
    for (int i = 0; i < entries.size(); ++i)
    {
        const auto& entry = entries.getReference (i);
        const auto at = "entries[" + juce::String (i) + "] ";

        if (hasArg (entry, "id"))
        {
            if (! ProjectEdits::findChannel (project, intArg (entry, "id")).isValid())
                return ControlResult::failure (at + noSuchChannel (intArg (entry, "id")));
        }
        else if (hasArg (entry, "kind") && ! kinds.contains (textArg (entry, "kind")))
        {
            return ControlResult::failure (at + "'" + textArg (entry, "kind")
                                           + "' is not a kind of channel. Use "
                                           + kinds.joinIntoString (", ") + ".");
        }
    }

    auto* undo = host.undoManager();
    beginOneTransaction (host, "channels_write");

    juce::Array<juce::var> touched;

    for (const auto& entry : entries)
    {
        auto channel = hasArg (entry, "id")
                           ? ProjectEdits::findChannel (project, intArg (entry, "id"))
                           : addOfKind (project, textArg (entry, "kind", "synth"),
                                        textArg (entry, "name", "Channel"), undo);

        if (! channel.isValid())
            continue;

        // A property absent from an entry is left alone rather than reset. The
        // difference between "set the name to empty" and "say nothing about the
        // name" is the whole of what makes this an upsert rather than a
        // replace, and without it updating one field would blank the rest.
        if (hasArg (entry, "name"))
            ProjectEdits::setProperty (channel, ids::name, textArg (entry, "name"), undo,
                                       "Rename channel", true);

        if (hasArg (entry, "mixerTrackId"))
            ProjectEdits::setProperty (channel, ids::mixerTrackId, intArg (entry, "mixerTrackId"),
                                       undo, "Route channel", true);

        if (hasArg (entry, ids::muted))
            ProjectEdits::setProperty (channel, ids::muted, flagArg (entry, ids::muted), undo,
                                       "Mute channel", true);

        if (hasArg (entry, "colour"))
            ProjectEdits::setColour (channel, textArg (entry, "colour"), undo, true);

        touched.add (Obj {}
                         .set ("id", (int) channel[ids::id])
                         .set ("name", channel[ids::name].toString())
                         .set ("source", channel[ids::source].toString()));
    }

    host.flushEngine();

    return ControlResult::success (
        Obj {}.set ("applied", touched.size()).set ("channels", arrayOf (touched)));
}

ControlResult remove (ControlHost& host, const juce::var& args)
{
    auto project = host.project();

    if (! project.isValid())
        return ControlResult::failure ("no project is open.");

    juce::Array<juce::ValueTree> doomed;

    for (const auto& id : arrayArg (args, "ids"))
    {
        const auto channel = ProjectEdits::findChannel (project, (int) id);

        if (! channel.isValid())
            return ControlResult::failure (noSuchChannel ((int) id));

        doomed.add (channel);
    }

    beginOneTransaction (host, "channels_remove");

    for (const auto& channel : doomed)
        ProjectEdits::removeChannel (project, channel, host.undoManager());

    host.flushEngine();

    return applied (doomed.size());
}

/** Points a channel at a file, and at a sound inside it.

    setSampleSource and setSoundFontSource rather than a property write, because
    what a channel stores about a file is more than its path: a sample's own
    rate and length are stored so a clip can be laid out before the audio has
    been read, and a soundfont's preset NAME is stored beside its bank and
    program so a channel can still say what it was pointed at on a machine that
    does not have the font.
*/
ControlResult writeSource (ControlHost& host, const juce::var& args)
{
    auto project = host.project();

    if (! project.isValid())
        return ControlResult::failure ("no project is open.");

    const auto channel = ProjectEdits::findChannel (project, intArg (args, "channelId"));

    if (! channel.isValid())
        return ControlResult::failure (noSuchChannel (intArg (args, "channelId")));

    const auto path = textArg (args, "file");
    const juce::File file { path };

    if (path.isNotEmpty() && ! file.existsAsFile())
        return ControlResult::failure ("there is no file at " + path + ".");

    auto* undo = host.undoManager();
    beginOneTransaction (host, "source_write");

    const auto source = channel[ids::source].toString();

    if (source == "soundfont")
    {
        const auto bank = intArg (args, "bank");
        const auto program = intArg (args, "program");
        const auto preset = textArg (args, "presetName");

        // Choosing a different sound in the font a channel already has must NOT
        // rewrite the path: a preset list is browsed with one file loaded, and
        // touching the path on every choice would make each of them a fresh
        // load of the whole font.
        if (path.isEmpty())
            ProjectEdits::setSoundFontPreset (channel, bank, program, preset, undo);
        else
            ProjectEdits::setSoundFontSource (channel, path, bank, program, preset, undo);
    }
    else if (source == "audio")
    {
        if (path.isEmpty())
            return ControlResult::failure ("an audio channel needs a file.");

        ProjectEdits::setSampleSource (channel, path, intArg (args, "sourceSampleRate"),
                                       intArg (args, "lengthSamples"), undo);
    }
    else
    {
        return ControlResult::failure ("channel " + juce::String (intArg (args, "channelId"))
                                       + " is a " + source
                                       + " channel, which plays no file. Only audio and soundfont "
                                         "channels have a source.");
    }

    host.flushEngine();

    return ControlResult::success (Obj {}.set ("done", true));
}

} // namespace

void appendChannelOps (std::vector<OpSpec>& all)
{
    all.push_back (
        { "channels_write",
          OpScope::write,
          "Add channels, or change the name, colour, routing or mute of existing ones, as one undo "
          "step.",
          "An upsert: an entry with an `id` changes that channel, an entry without one "
          "adds a channel of `kind`. A field an entry does not mention is left alone, "
          "so changing a name does not blank a colour.\n\n"
          "The sound a channel makes is not here - that is params_write, which reaches "
          "every oscillator, envelope and effect parameter it has. This is the channel "
          "as an object in the rack.",
          { { "entries", ValueKind::array, true, "The channels to add or change.",
              channelFields() } },
          write });

    all.push_back ({ "channels_remove",
                     OpScope::write,
                     "Remove channels and the notes that belong to them.",
                     "One undo step. A channel's notes live in patterns and become unreachable "
                     "once it is gone, so they go with it. Refuses the whole batch if any id "
                     "names nothing, rather than removing the ones it recognised.",
                     { { "ids",
                         ValueKind::array,
                         true,
                         "The channel ids to remove.",
                         { { "", ValueKind::integer, true, "A channel id." } } } },
                     remove });

    all.push_back (
        { "source_write",
          OpScope::write,
          "Point an audio channel at a sample file, or a soundfont channel at a file and a sound "
          "inside it.",
          "Only audio and soundfont channels have a source; a synth channel makes its "
          "own sound and this refuses it.\n\n"
          "For a soundfont, giving a bank and program with no `file` chooses a different "
          "sound inside the font the channel already has, which is deliberately not the "
          "same as re-pointing it: re-pointing reloads the whole font.\n\n"
          "A soundfont is referenced and never copied. A sample is gathered into the "
          "project's Assets folder when the project is saved.",
          { { "channelId", ValueKind::integer, true, "The channel to point." },
            { "file", ValueKind::text, false,
              "The path to the sample or soundfont. Omit to keep the current file." },
            { "sourceSampleRate", ValueKind::integer, false, "The sample's own rate, in Hz." },
            { "lengthSamples", ValueKind::integer, false, "How long the sample is, in samples." },
            { "bank", ValueKind::integer, false, "Soundfont bank. Drum kits are bank 128." },
            { "program", ValueKind::integer, false, "Soundfont program within the bank." },
            { "presetName", ValueKind::text, false,
              "What that sound is called, stored so the channel can name it when the "
              "font is missing." } },
          writeSource });
}

} // namespace dew::control
