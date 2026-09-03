#include "model/ScoreBake.h"

#include <cmath>

#include "model/Ids.h"
#include "model/Meter.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "model/ProjectSchema.h"

namespace dew
{

namespace
{

juce::ValueTree playlistOf (const juce::ValueTree& project)
{
    return project.getChildWithName (ids::PLAYLIST);
}

/** The lane a previous bake wrote to, or an invalid tree. */
juce::ValueTree findGeneratedTrack (const juce::ValueTree& project)
{
    for (const auto& track : playlistOf (project))
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

} // namespace

BakeReport ScoreBake::into (juce::ValueTree project, const lang::Score& score,
                            juce::UndoManager* undo)
{
    BakeReport report;

    if (! project.hasType (ids::PROJECT))
    {
        report.warnings.add ("not a project");
        return report;
    }

    const auto meter = Meter::of (project);

    // A grid mismatch is refused rather than applied. stepsPerBeat owns how
    // long a step IS - Transport divides by it - so changing it would keep
    // every existing note's step count and alter how fast the whole project
    // plays. Nothing is touched, and the caller is told why.
    if (meter.stepsPerBeat != score.stepsPerBeat)
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
    if (meter.beatsPerBar != score.beatsPerBar || meter.beatUnit != score.beatUnit)
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
    // calling it per edit is what turns a bake into a hundred undo steps.
    if (undo != nullptr)
        undo->beginNewTransaction ("Compile score");

    project.setProperty (ids::tempoBpm, score.tempoBpm, undo);

    // --- channels: adopt by name, create what is missing ---------------------
    std::vector<int> channelIdForTrack;
    channelIdForTrack.reserve (score.tracks.size());

    for (const auto& track : score.tracks)
    {
        auto channel = findChannelByName (project, track.name);

        if (channel.isValid())
        {
            // ADOPTED, not overwritten. The language owns notes; the user owns
            // the sound. Nothing but the name is read here.
            ++report.channelsAdopted;
        }
        else
        {
            channel = ProjectEdits::addChannel (project, track.name, undo);
            ++report.channelsCreated;
        }

        channelIdForTrack.push_back ((int) channel[ids::id]);
    }

    // --- clear what a previous bake left -------------------------------------
    auto lane = findGeneratedTrack (project);

    if (lane.isValid())
    {
        // Every clip on the lane we own, and every pattern only those clips
        // referred to. Patterns a user made are never touched, and their ids
        // are never renumbered.
        std::vector<int> ownedPatterns;

        for (const auto& clip : lane)
            if (clip.hasType (ids::CLIP) && ProjectEdits::isMidiClip (clip))
                ownedPatterns.push_back ((int) clip[ids::patternId]);

        while (lane.getNumChildren() > 0)
            lane.removeChild (lane.getNumChildren() - 1, undo);

        for (const auto patternId : ownedPatterns)
            if (auto pattern = ProjectEdits::findPattern (project, patternId);
                pattern.isValid())
                ProjectEdits::removePattern (project, pattern, undo);
    }
    else
    {
        lane = ProjectEdits::addPlaylistTrack (project, generatedTrackName(), undo);
    }

    // --- patterns -------------------------------------------------------------
    std::vector<int> patternIds;
    patternIds.reserve (score.patterns.size());

    for (const auto& pattern : score.patterns)
    {
        auto tree = ProjectEdits::addPattern (project, undo);
        tree.setProperty (ids::name, juce::String (pattern.name), undo);
        tree.setProperty (ids::lengthSteps, pattern.lengthSteps, undo);

        for (const auto& note : pattern.notes)
        {
            if (note.track < 0 || note.track >= (int) channelIdForTrack.size())
                continue;

            auto added = ProjectEdits::addNote (tree,
                                                channelIdForTrack[(std::size_t) note.track],
                                                note.startStep, note.lengthSteps,
                                                note.pitch, note.velocity, undo);

            // Rewritten as an exact three-decimal DOUBLE, which is not fussiness.
            // addNote takes a float, and 0.535f widens to 0.53500001430511475;
            // the serializer writes six decimals, so the file says "0.535" and
            // reading it back gives a different double. The JSON compares equal
            // either way - it is the TREE that stops round-tripping, which is
            // the kind of difference no amount of staring at the file reveals.
            added.setProperty (ids::velocity,
                               std::round ((double) note.velocity * 1000.0) / 1000.0,
                               undo);

            ++report.notesWritten;
        }

        patternIds.push_back ((int) tree[ids::id]);
        ++report.patternsWritten;
    }

    // --- clips ---------------------------------------------------------------
    for (const auto& clip : score.clips)
    {
        if (clip.pattern < 0 || clip.pattern >= (int) patternIds.size())
            continue;

        ProjectEdits::addClip (lane, patternIds[(std::size_t) clip.pattern],
                               clip.startBar, clip.lengthBars, undo);
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
