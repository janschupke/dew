// =============================================================================
// The meter, and the score source stored beside the music.
//
// One of seven translation units behind model/ProjectEdits.h. The header is
// one struct of static functions and stays where it was; this directory is
// where they are defined.
//
// Three edits that act on the PROJECT rather than on anything in it.
// Changing the meter rescales every clip, because a clip is stored in bars;
// changing the grid resolution rescales every NOTE, for the mirror-image
// reason - a note is stored in steps, and stepsPerBeat is what a step is worth;
// the score is text the compiler owns and the document carries.
// =============================================================================

#include "model/ProjectEdits.h"

#include "model/Ids.h"
#include "model/Meter.h"
#include "model/ProjectSchema.h"

namespace dew
{

void ProjectEdits::setGridResolution (juce::ValueTree project, int wanted, juce::UndoManager* undo)
{
    if (! project.isValid())
        return;

    const auto before = juce::jmax (1, (int) Meter::of (project).stepsPerBeat);
    const auto after = juce::jmax (1, wanted);

    if (after == before)
        return;

    // Steps in, time out, steps back - the shape setMeter's own rescale below
    // is written in, so there is one place to read the intent and no ratio to
    // get upside down.
    const auto rescaled = [before, after] (double steps)
    { return steps * (double) after / (double) before; };

    project.setProperty (ids::stepsPerBeat, after, undo);

    for (auto pattern : project)
    {
        if (! pattern.hasType (ids::PATTERN))
            continue;

        pattern.setProperty (
            ids::lengthSteps,
            juce::jmax (1, juce::roundToInt (rescaled ((int) pattern[ids::lengthSteps]))), undo);

        for (auto note : pattern)
        {
            if (! note.hasType (ids::NOTE))
                continue;

            note.setProperty (ids::step,
                              juce::jmax (0, juce::roundToInt (rescaled ((int) note[ids::step]))),
                              undo);
            note.setProperty (
                ids::lengthSteps,
                juce::jmax (1, juce::roundToInt (rescaled ((int) note[ids::lengthSteps]))), undo);
        }
    }

    // Clips too, now that they are stored in steps. This is the mirror image of
    // setMeter above, which used to rescale them and no longer does: a bar
    // changing size moves no clip, and a STEP changing size moves every one.
    for (auto track : project.getChildWithName (ids::PLAYLIST))
    {
        if (! track.hasType (ids::PLAYLIST_TRACK))
            continue;

        for (auto clip : track)
        {
            if (! clip.hasType (ids::CLIP))
                continue;

            clip.setProperty (
                ids::startStep,
                juce::jmax (0, juce::roundToInt (rescaled ((int) clip[ids::startStep]))), undo);
            clip.setProperty (
                ids::lengthSteps,
                juce::jmax (1, juce::roundToInt (rescaled ((int) clip[ids::lengthSteps]))), undo);
        }
    }

    // A DOUBLE, unlike a note's step, so it is rescaled without rounding - a
    // curve point is wherever the pointer left it, and rounding one to a step
    // would move every automated sweep in the project a little every time the
    // grid was touched.
    for (auto automation : project)
    {
        if (! automation.hasType (ids::AUTOMATION))
            continue;

        for (auto point : automation)
            if (point.hasType (ids::POINT))
                point.setProperty (ids::step, rescaled ((double) point[ids::step]), undo);
    }
}

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

    // No clip rescale, and its absence is the point. A clip is stored in STEPS,
    // so its position is already what the rescale here used to work to preserve
    // - and it preserves it exactly, where a loop that converted bars to bars
    // had to round whenever the ratio did not divide (16 to 12 is 4/3, and a
    // clip at bar 4 wanted bar 5.33).

    // Ceiling rather than rounding, and then grown to fit: the song is a
    // container counted in bars, and rounding it down would crop the
    // arrangement it holds.
    const auto songSteps = juce::jmax (1, (int) project[ids::barsInSong]) * oldStepsPerBar;
    const auto songBars = (songSteps + newStepsPerBar - 1) / newStepsPerBar;

    project.setProperty (ids::barsInSong, juce::jmax (1, songBars), undo);
    growSongToFitClips (project, undo);
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
