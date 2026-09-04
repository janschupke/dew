// =============================================================================
// The meter, and the score source stored beside the music.
//
// One of seven translation units behind model/ProjectEdits.h. The header is
// one struct of static functions and stays where it was; this directory is
// where they are defined.
//
// Two edits that act on the PROJECT rather than on anything in it.
// Changing the meter rescales every clip, because a clip is stored in
// bars; the score is text the compiler owns and the document carries.
// =============================================================================

#include "model/ProjectEdits.h"

#include "model/Ids.h"
#include "model/Meter.h"
#include "model/ProjectSchema.h"

namespace dew
{

void ProjectEdits::setMeter (juce::ValueTree project, int beatsPerBar, int beatUnit,
                             juce::UndoManager* undo, bool* wasExact)
{
    if (wasExact != nullptr)
        *wasExact = true;

    if (! project.isValid())
        return;

    const auto before = Meter::of (project);

    Meter after = before;
    after.beatsPerBar = juce::jlimit (1, Meter::maxBeatsPerBar, beatsPerBar);
    after.beatUnit = Meter::clampBeatUnit (beatUnit);

    if (after == before)
        return;

    const auto oldStepsPerBar = before.stepsPerBar();
    const auto newStepsPerBar = after.stepsPerBar();

    project.setProperty (ids::beatsPerBar, after.beatsPerBar, undo);
    project.setProperty (ids::beatUnit, after.beatUnit, undo);

    // beatUnit alone is notational, so it moves no bar line and nothing below
    // needs doing.
    if (oldStepsPerBar == newStepsPerBar)
        return;

    // Bars in, steps out, bars back: the rescale is expressed as "what step was
    // this, and which bar is that now" rather than as a ratio, so there is one
    // place to read the intent and no ratio to get upside down.
    const auto barsForSteps = [newStepsPerBar] (int steps)
    { return juce::roundToInt ((double) steps / (double) newStepsPerBar); };

    auto exact = true;

    for (auto track : project.getChildWithName (ids::PLAYLIST))
    {
        if (! track.hasType (ids::PLAYLIST_TRACK))
            continue;

        for (auto clip : track)
        {
            if (! clip.hasType (ids::CLIP))
                continue;

            const auto startSteps = juce::jmax (0, (int) clip[ids::startBar]) * oldStepsPerBar;
            const auto lengthSteps = juce::jmax (1, (int) clip[ids::lengthBars]) * oldStepsPerBar;

            exact = exact && startSteps % newStepsPerBar == 0 && lengthSteps % newStepsPerBar == 0;

            clip.setProperty (ids::startBar, juce::jmax (0, barsForSteps (startSteps)), undo);
            clip.setProperty (ids::lengthBars, juce::jmax (1, barsForSteps (lengthSteps)), undo);
        }
    }

    // Ceiling rather than rounding, and then grown to fit: the song is a
    // container, and rounding it down would crop the arrangement it holds.
    const auto songSteps = juce::jmax (1, (int) project[ids::barsInSong]) * oldStepsPerBar;
    const auto songBars = (songSteps + newStepsPerBar - 1) / newStepsPerBar;

    project.setProperty (ids::barsInSong, juce::jmax (1, songBars), undo);
    growSongToFitClips (project, undo);

    if (wasExact != nullptr)
        *wasExact = exact;
}

// --- the score ---------------------------------------------------------------

void ProjectEdits::setScoreSource (juce::ValueTree project, const juce::String& text,
                                   const juce::String& sourceName, juce::UndoManager* undo)
{
    auto score = project.getChildWithName (ids::SCORE);

    if (! score.isValid())
    {
        score = juce::ValueTree (ids::SCORE);
        project.appendChild (score, undo);
    }

    score.setProperty (ids::name, sourceName, undo);

    while (score.getNumChildren() > 0)
        score.removeChild (score.getNumChildren() - 1, undo);

    // Split by hand rather than with StringArray::addLines, which drops the
    // empty string after a trailing newline. That empty line is real - it is
    // the difference between a file that ends in a newline and one that does
    // not - and losing it would make saving a score silently rewrite it.
    const auto normalised = text.replace ("\r\n", "\n").replace ("\r", "\n");

    // No text is no lines, not one empty one - so a project nobody has written
    // a score for is byte-identical to one whose score was cleared.
    if (normalised.isEmpty())
        return;

    auto start = 0;

    for (;;)
    {
        const auto end = normalised.indexOfChar (start, '\n');
        const auto line = end < 0 ? normalised.substring (start)
                                  : normalised.substring (start, end);

        juce::ValueTree node (ids::LINE);
        node.setProperty (ids::text, line, nullptr);
        score.appendChild (node, undo);

        if (end < 0)
            break;

        start = end + 1;
    }
}

juce::String ProjectEdits::scoreSource (const juce::ValueTree& project)
{
    const auto score = project.getChildWithName (ids::SCORE);

    if (! score.isValid())
        return {};

    juce::StringArray lines;

    for (const auto& line : score)
        if (line.hasType (ids::LINE))
            lines.add (line[ids::text].toString());

    return lines.joinIntoString ("\n");
}

juce::String ProjectEdits::scoreSourceName (const juce::ValueTree& project)
{
    return project.getChildWithName (ids::SCORE)[ids::name].toString();
}

} // namespace dew
