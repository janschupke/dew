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

    /** Generated patterns somebody has edited since, left exactly as they are.

        Worth counting and worth saying out loud: silently keeping them looks
        like a compiler that ignored an edit to the score, and silently
        replacing them destroys work. The only honest option is to do one and
        report it.
    */
    int patternsKept = 0;

    /** Generated patterns the score no longer produces, removed. */
    int patternsRemoved = 0;

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
    /** What a recompile does with a generated pattern somebody has since
        edited by hand.
    */
    enum class Policy
    {
        keepHandEdits,   ///< leave it alone and report it. The default.
        discardHandEdits ///< replace it with what the score says. Explicit only.
    };

    /** Bakes into `project`, replacing what a previous bake left behind.

        Idempotent: baking the same score twice leaves the same document, with
        the same pattern ids, rather than a second copy of everything. Each
        compiled node carries a `genId` naming which part of the score produced
        it, so the second compile recognises its own work; a pattern also
        carries the hash its notes had when they were written, so an edit made
        in the piano roll since is visible and can be respected.

        Refuses, changing nothing, when the score's grid or meter disagrees with
        the project's - `stepsPerBeat` owns how long a step is, and
        ProjectEdits::setMeter rescales every clip and rounds, so applying
        either silently would move the user's existing arrangement.

        Unless the project holds no music at all, in which case it takes both:
        there is nothing there whose meaning they could change, and a fresh
        project sits at four steps per beat while almost every score needs
        twelve. Refusing there would make Compile do nothing on a new project.
    */
    static BakeReport into (juce::ValueTree project, const lang::Score&,
                            juce::UndoManager*, Policy = Policy::keepHandEdits);

    /** A fresh project holding only this score. What the CLI writes. */
    static juce::ValueTree toNewProject (const lang::Score&, BakeReport& report);

    /** The playlist track generated content lives on. One lane, because a dew
        PATTERN already holds every channel's notes for its span - which is
        exactly what a section is.
    */
    static const char* generatedTrackName() noexcept { return "Score"; }

    /** A fingerprint of what a pattern SOUNDS like: its length and its notes,
        in a fixed order so that reordering the children does not read as an
        edit. The name is deliberately excluded - renaming a pattern is not a
        musical change, and a compile should not have to give up updating one
        because somebody labelled it.
    */
    static juce::String patternHash (const juce::ValueTree& pattern);
};

} // namespace dew
