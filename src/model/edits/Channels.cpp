// =============================================================================
// Adding, removing and re-sourcing a channel; patterns.
//
// One of seven translation units behind model/ProjectEdits.h. The header is
// one struct of static functions and stays where it was; this directory is
// where they are defined.
//
// A channel and a pattern are the two things a project is made of, and
// removing either takes other things with it - a channel's notes, a
// pattern's clips - which is the whole reason both are here rather than
// written at the six call sites that wanted them.
// =============================================================================

#include "model/ProjectEdits.h"

#include "model/EntityColour.h"
#include "model/Ids.h"
#include "model/ModuleState.h"
#include "model/ProjectSchema.h"
#include "model/TreeWalk.h"

namespace dew
{

juce::ValueTree ProjectEdits::addChannel (juce::ValueTree project, const juce::String& name,
                                          juce::UndoManager* undo)
{
    auto channel = defaultTreeFor (childSpecFor (projectSpec(), "channels"));

    const auto id = nextFreeId (project, ids::CHANNEL);
    channel.setProperty (ids::id, id, nullptr);
    channel.setProperty (
        ids::name,
        name.isNotEmpty() ? name : tr (StringId::project_channelN, Args {}.with ("number", id)),
        nullptr);

    // Round the ramp rather than taking the schema default, which is one blue:
    // every channel a user added came out the same colour as the last, in an
    // application whose channel rack, step grid, piano roll, playlist and mixer
    // all identify a channel BY its colour.
    channel.setProperty (ids::colour, entityColour::defaultHex (id - 1), nullptr);

    // Route to a mixer track if one with a matching number exists, else insert 1.
    const auto mixer = project.getChildWithName (ids::MIXER);
    auto routed = tree::childWithId (mixer, ids::MIXER_TRACK, id);

    if (! routed.isValid())
        for (const auto& track : mixer)
            if (track.hasType (ids::MIXER_TRACK))
            {
                routed = track;
                break;
            }

    channel.setProperty (ids::mixerTrackId, routed.isValid() ? (int) routed[ids::id] : 1, nullptr);

    // Channels are a schema array: insert after the last existing one so the
    // canonical child order is preserved.
    int insertAt = project.getNumChildren();

    for (int i = 0; i < project.getNumChildren(); ++i)
        if (project.getChild (i).hasType (ids::CHANNEL))
            insertAt = i + 1;

    project.addChild (channel, insertAt, undo);
    return channel;
}

juce::ValueTree ProjectEdits::addAudioChannel (juce::ValueTree project, const juce::String& name,
                                               juce::UndoManager* undo)
{
    auto channel = addChannel (project, name.isNotEmpty() ? name : "Audio", undo);

    // After the insert, so the property change joins the same undo transaction
    // rather than becoming a second step that leaves a synth channel behind.
    channel.setProperty (ids::source, "audio", undo);
    return channel;
}

juce::ValueTree ProjectEdits::addSoundFontChannel (juce::ValueTree project,
                                                   const juce::String& name,
                                                   juce::UndoManager* undo)
{
    auto channel = addChannel (project, name.isNotEmpty() ? name : "SoundFont", undo);

    // In the same transaction, for the reason addAudioChannel does it here.
    channel.setProperty (ids::source, instrumentTypeToString (InstrumentType::soundfont), undo);
    return channel;
}

bool ProjectEdits::setInstrumentType (juce::ValueTree channel, InstrumentType type,
                                      juce::UndoManager* undo)
{
    if (! channel.hasType (ids::CHANNEL))
        return false;

    const auto wanted = instrumentTypeToString (type);

    if (channel[ids::source].toString() == wanted)
        return false;

    setProperty (channel, ids::source, wanted, undo, "Change instrument");
    return true;
}

std::optional<InstrumentType> ProjectEdits::instrumentTypeOf (const juce::ValueTree& channel)
{
    return instrumentTypeFor (channel[ids::source].toString());
}

bool ProjectEdits::playsNotes (const juce::ValueTree& channel)
{
    // An unknown source plays as a synth, which buildSnapshot already decided;
    // agreeing with it here is what keeps the editor and the engine showing the
    // same channel.
    const auto type = instrumentTypeOf (channel).value_or (InstrumentType::synth);
    return type == InstrumentType::synth || type == InstrumentType::soundfont;
}

bool ProjectEdits::playsClips (const juce::ValueTree& channel)
{
    return instrumentTypeOf (channel).value_or (InstrumentType::synth) == InstrumentType::audio;
}

void ProjectEdits::setSampleSource (juce::ValueTree channel, const juce::String& path,
                                    int sourceSampleRate, int lengthSamples,
                                    juce::UndoManager* undo)
{
    auto sample = channel.getChildWithName (ids::SAMPLE);

    if (! sample.isValid())
        return;

    sample.setProperty (ids::file, path, undo);
    sample.setProperty (ids::sourceSampleRate, juce::jmax (1, sourceSampleRate), undo);
    sample.setProperty (ids::lengthSamples, juce::jmax (0, lengthSamples), undo);

    // A new source invalidates the old trim, and leaving it would silence a
    // recording whose predecessor was trimmed to a shorter region.
    sample.setProperty (ids::startSample, 0, undo);
    sample.setProperty (ids::endSample, 0, undo);
}

void ProjectEdits::setSoundFontSource (juce::ValueTree channel, const juce::String& path, int bank,
                                       int program, const juce::String& presetName,
                                       juce::UndoManager* undo)
{
    auto node = channel.getChildWithName (ids::SOUNDFONT);

    if (! node.isValid())
        return;

    node.setProperty (ids::file, path, undo);
    setSoundFontPreset (channel, bank, program, presetName, undo);
}

void ProjectEdits::setSoundFontPreset (juce::ValueTree channel, int bank, int program,
                                       const juce::String& presetName, juce::UndoManager* undo)
{
    auto node = channel.getChildWithName (ids::SOUNDFONT);

    if (! node.isValid())
        return;

    node.setProperty (ids::bank, juce::jlimit (0, 128, bank), undo);
    node.setProperty (ids::program, juce::jlimit (0, 127, program), undo);
    node.setProperty (ids::presetName, presetName, undo);
}

void ProjectEdits::removeChannel (juce::ValueTree project, juce::ValueTree channel,
                                  juce::UndoManager* undo)
{
    if (! channel.isValid())
        return;

    const auto channelId = (int) channel[ids::id];

    // Notes referring to a channel that no longer exists would be dropped by
    // the next snapshot with a warning. Remove them with the channel instead,
    // so the document stays consistent and the change is one undo step.
    for (auto pattern : project)
    {
        if (! pattern.hasType (ids::PATTERN))
            continue;

        for (int i = pattern.getNumChildren(); --i >= 0;)
        {
            const auto note = pattern.getChild (i);

            if (note.hasType (ids::NOTE) && (int) note[ids::ch] == channelId)
                pattern.removeChild (i, undo);
        }
    }

    const auto index = project.indexOf (channel);

    if (index >= 0)
        project.removeChild (index, undo);
}

juce::ValueTree ProjectEdits::addPattern (juce::ValueTree project, juce::UndoManager* undo)
{
    auto pattern = defaultTreeFor (childSpecFor (projectSpec(), "patterns"));

    const auto id = nextFreeId (project, ids::PATTERN);
    pattern.setProperty (ids::id, id, nullptr);
    pattern.setProperty (ids::name, tr (StringId::project_patternN, Args {}.with ("number", id)),
                         nullptr);

    int insertAt = project.getNumChildren();

    for (int i = 0; i < project.getNumChildren(); ++i)
        if (project.getChild (i).hasType (ids::PATTERN))
            insertAt = i + 1;

    project.addChild (pattern, insertAt, undo);
    return pattern;
}

juce::ValueTree ProjectEdits::duplicatePattern (juce::ValueTree project, juce::ValueTree pattern,
                                                juce::UndoManager* undo)
{
    if (! pattern.isValid() || ! pattern.hasType (ids::PATTERN))
        return {};

    const auto index = project.indexOf (pattern);

    if (index < 0)
        return {};

    // createCopy is a deep copy, so the notes come along; only the identity has
    // to change.
    auto copy = pattern.createCopy();

    const auto sourceId = (int) pattern[ids::id];
    const auto newId = nextFreeId (project, ids::PATTERN);

    copy.setProperty (ids::id, newId, nullptr);

    // An auto-named pattern gets the next auto name; a renamed one keeps the
    // name it was given, marked as a copy, because that name is information.
    //
    // The comparison is against the auto name in the ACTIVE locale, so a
    // project made in one language and duplicated in another treats an
    // untouched pattern as renamed and calls the copy "Pattern 3 copy". A
    // stored flag is the only thing that would answer "was this renamed"
    // properly, and inventing one for this is a file-format change to improve a
    // name. Written down in .ai/rules/i18n.md rather than papered over.
    const auto sourceName = pattern[ids::name].toString();
    const auto autoName = tr (StringId::project_patternN, Args {}.with ("number", sourceId));

    copy.setProperty (ids::name,
                      sourceName == autoName
                          ? tr (StringId::project_patternN, Args {}.with ("number", newId))
                          : tr (StringId::project_copyOf, Args {}.with ("name", sourceName)),
                      nullptr);

    // Next to the original, so the pattern list reads in the order it was built.
    project.addChild (copy, index + 1, undo);
    return copy;
}

bool ProjectEdits::removePattern (juce::ValueTree project, juce::ValueTree pattern,
                                  juce::UndoManager* undo)
{
    if (! pattern.isValid() || ! pattern.hasType (ids::PATTERN))
        return false;

    const auto index = project.indexOf (pattern);

    if (index < 0)
        return false;

    int patternCount = 0;

    for (const auto& child : project)
        if (child.hasType (ids::PATTERN))
            ++patternCount;

    // A project with no patterns has nothing to edit and nothing to play.
    if (patternCount <= 1)
        return false;

    const auto patternId = (int) pattern[ids::id];

    // Clips referring to a pattern that no longer exists would be dropped by the
    // next snapshot with a warning. Remove them here so the document stays
    // consistent and the whole deletion is one undo step.
    const auto playlist = project.getChildWithName (ids::PLAYLIST);

    for (auto track : playlist)
    {
        if (! track.hasType (ids::PLAYLIST_TRACK))
            continue;

        for (int i = track.getNumChildren(); --i >= 0;)
        {
            const auto clip = track.getChild (i);

            if (clip.hasType (ids::CLIP) && (int) clip[ids::patternId] == patternId)
                track.removeChild (i, undo);
        }
    }

    project.removeChild (index, undo);
    return true;
}

int ProjectEdits::lengthNeededForNotes (const juce::ValueTree& pattern)
{
    int needed = 1;

    for (const auto& note : pattern)
        if (note.hasType (ids::NOTE))
            needed = juce::jmax (needed, (int) note[ids::step] + (int) note[ids::lengthSteps]);

    return needed;
}

} // namespace dew
