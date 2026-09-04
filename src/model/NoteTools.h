#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace dew
{

/** Grid divisions the editors can snap to.

    Musical divisions rather than literal step counts: a step is a subdivision of
    a beat, `stepsPerBeat` is a project property, and the ruler already derives
    bar lines from it. Hard-coding 16 for a bar would make the dropdown lie about
    any project that did not use the default.
*/
enum class SnapDivision
{
    sixteenth,
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
    static constexpr int numSnapDivisions = 5;

    static const SnapDivision allSnapDivisions[numSnapDivisions];

    /** Steps in one grid cell. Never less than one: a zero-width grid would make
        every snap a division by zero, and a step is already the finest position
        a note can hold.
    */
    static int stepsForSnap (SnapDivision, int stepsPerBeat, int beatsPerBar = 4) noexcept;

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
                         int snapSteps, juce::UndoManager*);

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
        int stepAmount = 0;           ///< +/- around each note's start, in steps
    };

    /** Disturbs velocity and start step, once, in place.

        `random` is a parameter rather than a local so a test can seed it and
        assert exact output; production passes a time-seeded one.

        A note pushed before the start of the pattern is clamped to step 0 rather
        than reflected - clamping keeps the operation total, and a humanise pass
        that deleted notes near the start would be a surprise.
    */
    static void randomize (juce::ValueTree pattern, const juce::Array<juce::ValueTree>& notes,
                           const RandomizeOptions&, juce::Random& random, juce::UndoManager*);
};

} // namespace dew
