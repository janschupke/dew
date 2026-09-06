#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace dew
{

/** Grid divisions the editors can snap to, finest first.

    Musical divisions rather than literal step counts: a step is a subdivision of
    a beat, `stepsPerBeat` is a project property, and the ruler already derives
    bar lines from it. Hard-coding 16 for a bar would make the dropdown lie about
    any project that did not use the default.

    Not every division fits every grid, which is the point of having more of them
    than a default project can use. At four steps to a beat a sixteenth IS a
    step, so every finer division and every triplet falls between two of them -
    and `fitsGrid` is what lets the dropdown grey those out rather than round
    them to something else and claim it snapped. Raising the project's grid
    resolution is what turns them on; see ProjectEdits::setGridResolution.
*/
enum class SnapDivision
{
    /** No grid at all: a note goes where the pointer put it.

        Which a step-based document CAN express, unlike the finer divisions - a
        step is the finest position a note holds, so "off" and "one step" are
        the same placement. It is here because they are not the same INTENT:
        every other division is a promise that things line up, and this is the
        one entry that promises they will not.
    */
    off,

    thirtysecond,
    sixteenthTriplet,
    sixteenth,
    eighthTriplet,
    eighth,
    quarter,
    half,
    bar
};

/** Bulk note manipulation, separated from the piano roll that drives it.

    None of this needs a component to be correct, and PianoRollComponent is
    already long enough that burying the semantics in a mouse handler would make
    them untestable in practice. Everything here mutates through ProjectEdits, so
    undo still covers every edit, and every operation takes the notes it should
    act on rather than working out the scope itself - the scope rule is one
    function, `scopeFor`, used by every caller.
*/
struct NoteTools
{
    /** The pitches a dew document may hold, C0 to C8.

        Here rather than on the piano roll, which is where they were, for the
        reason ProjectEdits::minPointGap is in the model and not in Tokens.h:
        it is a rule about what a document may CONTAIN, not a decision about
        what a view can draw. The roll still spells them its own way and gets
        them from here, and transpose - which clamps a group against them - is
        in this file, so the rule and its enforcement now sit together.

        It matters beyond tidiness: anything that writes notes without going
        through the roll needs the same bounds, and a note outside them is one
        the editor cannot show.
    */
    static constexpr int lowestPitch = 12;
    static constexpr int highestPitch = 108;

    // --- snapping ------------------------------------------------------------
    static constexpr int numSnapDivisions = 9;

    static const SnapDivision allSnapDivisions[numSnapDivisions];

    /** Steps in one grid cell. Never less than one: a zero-width grid would make
        every snap a division by zero, and a step is already the finest position
        a note can hold.

        A division the grid cannot express answers one, which is the identity -
        so a caller that ignores `fitsGrid` gets the old behaviour rather than a
        crash. The dropdown does not ignore it.
    */
    static int stepsForSnap (SnapDivision, int stepsPerBeat, int beatsPerBar = 4) noexcept;

    /** Whether this division lands on whole steps at this grid, and is coarser
        than one step.

        Both halves matter and they fail differently. A triplet at four steps to
        a beat falls BETWEEN steps, so snapping to it would move a note
        somewhere the division does not name. A sixteenth at four steps to a
        beat IS a step, so snapping to it moves nothing - which is why Quantize
        appeared to do nothing at all in a default project, and is the defect
        this predicate exists to make visible.

        `off` fits every grid, being the absence of one.
    */
    static bool fitsGrid (SnapDivision, int stepsPerBeat, int beatsPerBar = 4) noexcept;

    /** `wanted` if this grid can express it, and otherwise the next COARSER
        division that it can.

        Coarser rather than finer because a coarser grid is always expressible -
        the bar is the floor - and because rounding the other way would answer
        with a division that lands between steps, which is the thing fitsGrid
        exists to refuse. `off` is returned unchanged; it fits every grid.

        Used when the project's grid RESOLUTION changes under a division that
        was fine on the old one: leaving it selected would be a dropdown showing
        a setting that is not in force.
    */
    static SnapDivision nearestFittingSnap (SnapDivision wanted, int stepsPerBeat,
                                            int beatsPerBar = 4) noexcept;

    /** Whether this division snaps at all. False only for `off`, and named so
        the call sites read as the question they are asking rather than as a
        comparison against one enumerator.
    */
    static bool snaps (SnapDivision division) noexcept
    {
        return division != SnapDivision::off;
    }

    /** The grid resolutions a project can be set to, in steps per beat.

        Four rungs rather than a free number, and each one is what it BUYS: 4
        places sixteenths, 8 adds thirty-seconds, 12 adds triplets, 24 adds
        both. A grid nothing can be expressed on is not a grid, and a spin box
        of arbitrary integers would offer thirty of them.
    */
    static constexpr int numGridResolutions = 4;

    static const int gridResolutions[numGridResolutions];

    /** What a grid is called: the finest division it can place. */
    static juce::String nameForGrid (int stepsPerBeat, int beatUnit = 4);

    /** The division's label. `beatUnit` is the project's notational denominator:
        the divisions are relative to a BEAT, so "quarter" is one beat and reads
        as 1/4 only while a beat is a quarter note. In 6/8 the same division is
        an eighth, and a fixed label would lie about it.
    */
    static juce::String nameForSnap (SnapDivision, int beatUnit = 4);

    /** The division's ordinal, for persisting it. Out-of-range reads fall back
        to `sixteenth` rather than restoring a grid that does not exist.
    */
    static SnapDivision snapFromIndex (int index) noexcept;
    static int indexOfSnap (SnapDivision) noexcept;

    /** Down to the start of the cell this step is in - where a drawn note goes,
        because clicking anywhere in a cell means that cell.
    */
    static int snapFloor (int step, int snapSteps) noexcept;

    /** To the closest grid line - where a dragged or quantized note goes. */
    static int snapNearest (int step, int snapSteps) noexcept;

    /** Up to the end of the cell this step is in - where a length goes, so a
        note is never shorter than the grid it was drawn on.
    */
    static int snapCeil (int step, int snapSteps) noexcept;

    // --- lookup --------------------------------------------------------------
    /** Every note of this channel in this pattern, in tree order. */
    static juce::Array<juce::ValueTree> notesOnChannel (const juce::ValueTree& pattern,
                                                        int channelId);

    /** What an edit applies to: the selection when it has any, otherwise every
        note on the channel.

        One function rather than the same three lines in five call sites, because
        an operation that quietly disagreed with the others about its scope would
        be a bug nobody could see.
    */
    static juce::Array<juce::ValueTree> scopeFor (const juce::ValueTree& pattern, int channelId,
                                                  const juce::Array<juce::ValueTree>& selection);

    /** The note on this channel covering this step at this pitch, or an invalid
        tree. Unlike ProjectEdits::findNote this matches a note the step falls
        *inside*, not one that starts exactly there - which is what painting
        needs, so a brush stroke does not write inside a note it is crossing.
    */
    static juce::ValueTree noteCovering (const juce::ValueTree& pattern, int channelId, int step,
                                         int pitch);

    // --- operations ----------------------------------------------------------
    /** Splits `note` in two at `atStep`, returning the tail.

        The head is the original tree, resized - so a caller holding a pointer to
        it, and any selection containing it, survive the cut.

        Returns an invalid tree and changes nothing unless the cut falls strictly
        inside the note. A one-step note has no interior boundary, and the
        alternative is a zero-length note that resizeNote would silently clamp
        back to one, leaving a duplicate behind.
    */
    static juce::ValueTree sliceNote (juce::ValueTree pattern, juce::ValueTree note, int atStep,
                                      juce::UndoManager*);

    /** Rounds each note's start to the nearest grid line, and returns how many
        notes were removed as duplicates.

        Length is deliberately left alone. Quantizing length as well turns an
        eighth-note line quantized to 1/4 into a legato blur, and makes the
        operation change how long things sound rather than only when they start.

        Two notes that land on the same step and pitch on the same channel are
        collapsed into one, keeping the longer length: they retrigger one voice
        at one instant, so the second is inaudible, invisible, and grows the file
        every time the pattern is quantized again.
    */
    static int quantize (juce::ValueTree pattern, const juce::Array<juce::ValueTree>& notes,
                         int snapSteps, int stepsPerBar, juce::UndoManager*);

    /** Shifts every note by the same interval, clamped as a group so a chord
        keeps its intervals against the pitch limits instead of compressing.

        Returns the interval actually applied, which is zero when the group is
        already against a limit - so the caller can skip an empty undo
        transaction rather than filling the stack with edits that did nothing.
    */
    static int transpose (const juce::Array<juce::ValueTree>& notes, int semitones, int minPitch,
                          int maxPitch, juce::UndoManager*);

    /** How much to disturb each note, and what to leave alone.

        An amount of zero means "do not touch this property", so there are no
        separate enable flags that could disagree with the amounts beside them.
    */
    struct RandomizeOptions
    {
        double velocityAmount = 0.25; ///< +/- around each note's velocity, in velocity units

        /** +/- around each note's start, in steps.

            One rather than zero. At zero the default apply moved nothing at all
            - it changed velocity, which shows only as the alpha of a note and a
            bar in the lane below - so the honest reading of pressing Randomize
            and watching the grid was that the button did nothing. A step is the
            smallest disturbance the document can hold, which is the right
            default for a humanise pass.
        */
        int stepAmount = 1;
    };

    /** Disturbs velocity and start step, once, in place.

        `random` is a parameter rather than a local so a test can seed it and
        assert exact output; production passes a time-seeded one.

        A note pushed before the start of the pattern is clamped to step 0 rather
        than reflected - clamping keeps the operation total, and a humanise pass
        that deleted notes near the start would be a surprise.
    */
    static void randomize (juce::ValueTree pattern, const juce::Array<juce::ValueTree>& notes,
                           const RandomizeOptions&, juce::Random& random, int stepsPerBar,
                           juce::UndoManager*);
};

} // namespace dew
