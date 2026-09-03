#include "model/ScoreBake.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "lang/Rng.h"
#include "model/Ids.h"
#include "model/Meter.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "model/ProjectSchema.h"

namespace dew
{

namespace
{

/** The identity a compiled node carries, so a later compile can find it again.

    Prefixed by what it is, because the four kinds live in one document and a
    channel called `verse#0` and a pattern called `verse#0` are not the same
    thing.
*/
juce::String channelGenId (const juce::String& trackName)
{
    return "channel:" + trackName.toLowerCase();
}

juce::String laneGenId()
{
    return "lane:score";
}

juce::ValueTree playlistOf (const juce::ValueTree& project)
{
    return project.getChildWithName (ids::PLAYLIST);
}

/** The first child of `parent` of this type whose `genId` matches. */
juce::ValueTree findByGenId (const juce::ValueTree& parent, const juce::Identifier& type,
                             const juce::String& id)
{
    if (id.isEmpty())
        return {};

    for (const auto& child : parent)
        if (child.hasType (type) && child[ids::genId].toString() == id)
            return child;

    return {};
}

/** The lane a previous bake wrote to.

    By provenance first and by name second, so renaming the lane does not make
    the next compile create a second one - and so a lane a user happened to call
    "Score" is adopted rather than duplicated.
*/
juce::ValueTree findGeneratedTrack (const juce::ValueTree& project)
{
    const auto playlist = playlistOf (project);

    if (auto owned = findByGenId (playlist, ids::PLAYLIST_TRACK, laneGenId()); owned.isValid())
        return owned;

    for (const auto& track : playlist)
        if (track.hasType (ids::PLAYLIST_TRACK)
            && track[ids::name].toString() == ScoreBake::generatedTrackName())
            return track;

    return {};
}

/** Matches by name, case-insensitively, so a user who renamed a channel to
    match their score gets it adopted rather than duplicated.
*/
juce::ValueTree findChannelByName (const juce::ValueTree& project, const juce::String& name)
{
    for (const auto& channel : project)
        if (channel.hasType (ids::CHANNEL)
            && channel[ids::name].toString().equalsIgnoreCase (name))
            return channel;

    return {};
}

/** True if there is any music in this project at all.

    A project with no notes and no clips has nothing whose meaning a grid or a
    meter change could alter, so a score may set both. That distinction is what
    makes File > New, paste a score, Compile actually work: a fresh project sits
    at four steps per beat, almost every score needs twelve, and refusing there
    would mean the button did nothing and explained why in a status bar.
*/
bool hasMusic (const juce::ValueTree& project)
{
    for (const auto& child : project)
    {
        if (child.hasType (ids::PATTERN))
            for (const auto& note : child)
                if (note.hasType (ids::NOTE))
                    return true;

        if (child.hasType (ids::AUTOMATION))
            return true;
    }

    for (const auto& track : project.getChildWithName (ids::PLAYLIST))
        for (const auto& clip : track)
            if (clip.hasType (ids::CLIP))
                return true;

    return false;
}

/** Velocity as the document stores it: an exact three-decimal double.

    Not fussiness. ProjectEdits::addNote takes a float, and 0.535f widens to
    0.53500001430511475; the serializer writes six decimal places, so the file
    says "0.535" and reading it back gives a DIFFERENT double. The JSON compares
    equal either way - it is the tree that stops round-tripping, which is the
    kind of difference no amount of staring at the file reveals.
*/
double storedVelocity (float velocity)
{
    return std::round ((double) velocity * 1000.0) / 1000.0;
}

/** Replaces a pattern's notes with the score's, and reports how many.

    Whole-payload replacement rather than a diff: the notes ARE the compiler's
    output, and a diff would only be a slower way to reach the same tree while
    adding a second place for the two to disagree.
*/
int writeNotes (juce::ValueTree pattern, const lang::PatternDesc& desc,
                const std::vector<int>& channelIdForTrack, juce::UndoManager* undo)
{
    pattern.setProperty (ids::name, juce::String (desc.name), undo);
    pattern.setProperty (ids::lengthSteps, desc.lengthSteps, undo);

    while (pattern.getNumChildren() > 0)
        pattern.removeChild (pattern.getNumChildren() - 1, undo);

    auto written = 0;

    for (const auto& note : desc.notes)
    {
        if (note.track < 0 || note.track >= (int) channelIdForTrack.size())
            continue;

        auto added = ProjectEdits::addNote (pattern,
                                            channelIdForTrack[(std::size_t) note.track],
                                            note.startStep, note.lengthSteps,
                                            note.pitch, note.velocity, undo);

        added.setProperty (ids::velocity, storedVelocity (note.velocity), undo);
        ++written;
    }

    return written;
}

} // namespace

juce::String ScoreBake::patternHash (const juce::ValueTree& pattern)
{
    if (! pattern.isValid())
        return {};

    // Sorted, so the order the notes happen to sit in is not part of the
    // fingerprint. Moving a note in the piano roll changes what a pattern
    // sounds like; the order its children were appended in does not.
    std::vector<std::array<int, 5>> notes;

    for (const auto& note : pattern)
        if (note.hasType (ids::NOTE))
            notes.push_back ({ (int) note[ids::ch], (int) note[ids::step],
                               (int) note[ids::lengthSteps], (int) note[ids::pitch],
                               (int) std::lround ((double) note[ids::velocity] * 1000.0) });

    std::sort (notes.begin(), notes.end());

    juce::String payload { (int) pattern[ids::lengthSteps] };

    for (const auto& note : notes)
        for (const auto field : note)
            payload << ":" << field;

    return juce::String::toHexString ((juce::int64) lang::fnv1a64 (payload.toStdString()));
}

BakeReport ScoreBake::into (juce::ValueTree project, const lang::Score& score,
                            juce::UndoManager* undo, Policy policy)
{
    BakeReport report;

    if (! project.hasType (ids::PROJECT))
    {
        report.warnings.add ("not a project");
        return report;
    }

    const auto meter = Meter::of (project);
    const auto empty = ! hasMusic (project);

    // An empty project takes the score's grid and meter, because there is
    // nothing in it whose meaning they could change.
    if (empty)
    {
        if (undo != nullptr)
            undo->beginNewTransaction ("Compile score");

        project.setProperty (ids::stepsPerBeat, score.stepsPerBeat, undo);
        project.setProperty (ids::beatsPerBar, score.beatsPerBar, undo);
        project.setProperty (ids::beatUnit, score.beatUnit, undo);
    }

    // A grid mismatch is refused rather than applied. stepsPerBeat owns how
    // long a step IS - Transport divides by it - so changing it would keep
    // every existing note's step count and alter how fast the whole project
    // plays. Nothing is touched, and the caller is told why.
    if (! empty && meter.stepsPerBeat != score.stepsPerBeat)
    {
        report.warnings.add ("the score needs a grid of " + juce::String (score.stepsPerBeat)
                             + " steps per beat and the project has "
                             + juce::String (meter.stepsPerBeat)
                             + "; changing it would alter playback speed, so nothing was written");
        return report;
    }

    // Same for the meter: ProjectEdits::setMeter rescales every clip and rounds,
    // so a score that could set it is a score that can silently move an
    // arrangement it did not write.
    if (! empty && (meter.beatsPerBar != score.beatsPerBar || meter.beatUnit != score.beatUnit))
    {
        report.warnings.add ("the score is in " + juce::String (score.beatsPerBar) + "/"
                             + juce::String (score.beatUnit) + " and the project is in "
                             + meter.toString()
                             + "; changing the meter would move every existing clip, "
                               "so nothing was written");
        return report;
    }

    // ONE transaction, opened before the first mutation and never re-opened:
    // beginNewTransaction ARMS a transaction rather than being a no-op, so
    // calling it per edit is what turns a bake into a hundred undo steps. The
    // empty-project branch above opened it already, for the same reason.
    if (undo != nullptr && ! empty)
        undo->beginNewTransaction ("Compile score");

    project.setProperty (ids::tempoBpm, score.tempoBpm, undo);

    // --- channels: by provenance, then by name, then create -------------------
    std::vector<int> channelIdForTrack;
    channelIdForTrack.reserve (score.tracks.size());

    for (const auto& track : score.tracks)
    {
        const auto name = juce::String (track.name);
        const auto gen = channelGenId (name);

        // By provenance first: a channel this score made before is found again
        // even though it has been renamed since, and renaming a generated
        // channel must not spawn a duplicate. Then by name, which is how a
        // channel the user made joins the score in the first place.
        auto channel = findByGenId (project, ids::CHANNEL, gen);

        if (! channel.isValid())
            channel = findChannelByName (project, name);

        if (channel.isValid())
        {
            // ADOPTED, not overwritten. The language owns notes; the user owns
            // the sound. Nothing but the name and the provenance is read or
            // written here.
            ++report.channelsAdopted;
        }
        else
        {
            channel = ProjectEdits::addChannel (project, name, undo);
            ++report.channelsCreated;
        }

        channel.setProperty (ids::genId, gen, undo);
        channelIdForTrack.push_back ((int) channel[ids::id]);
    }

    // --- what a previous bake left --------------------------------------------
    // Sorted into the three buckets before anything is written, because the
    // decision about a pattern depends on its state BEFORE this compile
    // touches it.
    struct Existing
    {
        juce::String key;
        juce::ValueTree tree;
        bool handEdited = false;
        bool claimed = false;
    };

    // A list rather than a map keyed on genId, because duplicating a generated
    // pattern in the pattern list copies its provenance too - and a map would
    // silently forget one of the two, leaving a stray that no later compile
    // could ever see again.
    std::vector<Existing> generated;

    for (const auto& pattern : project)
    {
        if (! pattern.hasType (ids::PATTERN))
            continue;

        const auto gen = pattern[ids::genId].toString();

        if (gen.isEmpty())
            continue; // somebody made this by hand; it is not ours to touch

        generated.push_back ({ gen, pattern,
                               policy == Policy::keepHandEdits
                                   && patternHash (pattern) != pattern[ids::genHash].toString(),
                               false });
    }

    auto lane = findGeneratedTrack (project);

    if (! lane.isValid())
        lane = ProjectEdits::addPlaylistTrack (project, generatedTrackName(), undo);

    lane.setProperty (ids::genId, laneGenId(), undo);

    // The lane's clips are rebuilt wholesale. A clip carries nothing a person
    // can have put into it - where a section sits is the arrangement's to say -
    // so there is nothing here to preserve. A generated clip DRAGGED to another
    // lane is a different matter: it is not on this lane, so it is left alone.
    while (lane.getNumChildren() > 0)
        lane.removeChild (lane.getNumChildren() - 1, undo);

    // --- patterns -------------------------------------------------------------
    std::vector<int> patternIds;
    patternIds.reserve (score.patterns.size());

    for (const auto& desc : score.patterns)
    {
        const auto key = juce::String (desc.key);

        const auto found = std::find_if (generated.begin(), generated.end(),
                                         [&key] (const Existing& e)
                                         { return ! e.claimed && e.key == key; });

        if (found != generated.end())
        {
            found->claimed = true;

            if (found->handEdited)
            {
                // Kept exactly as it is, notes and hash alike. Refreshing the
                // hash here would quietly adopt the edit and let the NEXT
                // compile overwrite it - the bug that makes "your edits are
                // safe" true only once.
                patternIds.push_back ((int) found->tree[ids::id]);
                ++report.patternsKept;
                continue;
            }

            auto pattern = found->tree;
            report.notesWritten += writeNotes (pattern, desc, channelIdForTrack, undo);
            pattern.setProperty (ids::genHash, patternHash (pattern), undo);

            patternIds.push_back ((int) pattern[ids::id]);
            ++report.patternsWritten;
            continue;
        }

        auto pattern = ProjectEdits::addPattern (project, undo);
        pattern.setProperty (ids::genId, key, undo);

        report.notesWritten += writeNotes (pattern, desc, channelIdForTrack, undo);
        pattern.setProperty (ids::genHash, patternHash (pattern), undo);

        patternIds.push_back ((int) pattern[ids::id]);
        ++report.patternsWritten;
    }

    // --- what the score no longer produces ------------------------------------
    for (auto& existing : generated)
    {
        if (existing.claimed)
            continue;

        if (existing.handEdited)
        {
            // An orphan somebody has worked on. Removing it would throw that
            // work away over a section rename, so it stays - without a clip,
            // in the pattern list, where it can be found.
            ++report.patternsKept;
            continue;
        }

        if (ProjectEdits::removePattern (project, existing.tree, undo))
            ++report.patternsRemoved;
    }

    // --- clips ----------------------------------------------------------------
    for (const auto& clip : score.clips)
    {
        if (clip.pattern < 0 || clip.pattern >= (int) patternIds.size())
            continue;

        auto added = ProjectEdits::addClip (lane, patternIds[(std::size_t) clip.pattern],
                                            clip.startBar, clip.lengthBars, undo);
        added.setProperty (ids::genId, juce::String (clip.key), undo);
        ++report.clipsWritten;
    }

    // Grow, never shrink - trailing empty bars are a deliberate silence, which
    // is the rule the rest of the editor already follows.
    ProjectEdits::growSongToFitClips (project, undo);

    return report;
}

juce::ValueTree ScoreBake::toNewProject (const lang::Score& score, BakeReport& report)
{
    auto project = ProjectFactory::createDefault();

    // A fresh project starts at four steps per beat; the score's grid is the
    // one that matters here, and there is nothing yet whose speed it could
    // change. Same for the meter.
    project.setProperty (ids::stepsPerBeat, score.stepsPerBeat, nullptr);
    project.setProperty (ids::beatsPerBar, score.beatsPerBar, nullptr);
    project.setProperty (ids::beatUnit, score.beatUnit, nullptr);
    project.setProperty (ids::name, juce::String (score.title), nullptr);

    report = into (project, score, nullptr);

    return canonicalTree (project, projectSpec());
}

} // namespace dew
