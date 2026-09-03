#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include "lang/Score.h"

namespace dew
{

struct BakeReport
{
    int channelsCreated = 0;
    int channelsAdopted = 0;
    int patternsWritten = 0;
    int clipsWritten = 0;
    int notesWritten = 0;

    juce::StringArray warnings;
};

/** Writes a compiled score into a project.

    Ordinary channels, patterns, notes and `kind="pattern"` clips - nothing the
    rest of dew has to be taught about. That is the whole point of baking rather
    than evaluating at snapshot time: playback, the piano roll, the renderer,
    stems and MIDI export all work on the result without a line of change, and
    no new clip kind has to be threaded through the four places that would
    otherwise need it.

    THE OWNERSHIP LINE: the language owns notes, patterns and clips. The user
    owns channels, instruments, effects and the mixer. A track resolves to a
    channel by name and ADOPTS it - the bake sets nothing on a channel but its
    name - so dialling in a sound and then recompiling cannot lose it.

    Everything happens inside one UndoManager transaction, so a bake is one
    undo step.
*/
struct ScoreBake
{
    /** Bakes into `project`, replacing what a previous bake left behind.

        Refuses, changing nothing, when the score's grid or meter disagrees with
        the project's - `stepsPerBeat` owns how long a step is, and
        ProjectEdits::setMeter rescales every clip and rounds, so applying
        either silently would move the user's existing arrangement.
    */
    static BakeReport into (juce::ValueTree project, const lang::Score&,
                            juce::UndoManager*);

    /** A fresh project holding only this score. What the CLI writes. */
    static juce::ValueTree toNewProject (const lang::Score&, BakeReport& report);

    /** The playlist track generated content lives on. One lane, because a dew
        PATTERN already holds every channel's notes for its span - which is
        exactly what a section is.
    */
    static const char* generatedTrackName() noexcept { return "Score"; }
};

} // namespace dew
