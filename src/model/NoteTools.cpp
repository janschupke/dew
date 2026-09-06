#include "model/NoteTools.h"

#include "model/Ids.h"
#include "model/ProjectEdits.h"

namespace dew
{

const SnapDivision NoteTools::allSnapDivisions[NoteTools::numSnapDivisions] = {
    SnapDivision::off,       SnapDivision::thirtysecond,  SnapDivision::sixteenthTriplet,
    SnapDivision::sixteenth, SnapDivision::eighthTriplet, SnapDivision::eighth,
    SnapDivision::quarter,   SnapDivision::half,          SnapDivision::bar
};

const int NoteTools::gridResolutions[NoteTools::numGridResolutions] = { 4, 8, 12, 24 };

namespace
{

/** A division as a fraction of a beat: numerator over denominator.

    Written as the fraction rather than as a step count so `fitsGrid` and
    `stepsForSnap` answer from ONE statement of what a division IS. They used to
    be the same switch twice, which is two places for a triplet to be wrong in.
*/
struct BeatFraction
{
    int numerator = 1;
    int denominator = 1;
};

BeatFraction fractionOf (SnapDivision division) noexcept
{
    switch (division)
    {
        case SnapDivision::off: return { 0, 0 }; ///< not a fraction of a beat either
        case SnapDivision::thirtysecond: return { 1, 8 };
        case SnapDivision::sixteenthTriplet: return { 1, 6 };
        case SnapDivision::sixteenth: return { 1, 4 };
        case SnapDivision::eighthTriplet: return { 1, 3 };
        case SnapDivision::eighth: return { 1, 2 };
        case SnapDivision::quarter: return { 1, 1 };
        case SnapDivision::half: return { 2, 1 };
        case SnapDivision::bar: return { 0, 0 }; ///< the bar is not a fraction of a beat
    }

    return { 1, 4 };
}

/** Whether a division lands on whole steps at all, which is a weaker question
    than fitsGrid asks and the right one for NAMING a grid.

    At eight steps to a beat a thirty-second IS a step: you can place one, which
    is what the grid gets its name from, but snapping to it would move nothing -
    which is why fitsGrid refuses it and this does not.
*/
bool placesOnGrid (SnapDivision division, int stepsPerBeat) noexcept
{
    const auto fraction = fractionOf (division);
    const auto cell = juce::jmax (1, stepsPerBeat) * fraction.numerator;

    return fraction.denominator > 0 && cell % fraction.denominator == 0
           && cell / fraction.denominator >= 1;
}

} // namespace

int NoteTools::stepsForSnap (SnapDivision division, int stepsPerBeat, int beatsPerBar) noexcept
{
    const auto perBeat = juce::jmax (1, stepsPerBeat);

    // One step, which is the finest position a note can hold and therefore the
    // identity. Not the quarter its beat fraction would give: `off` shares that
    // fraction because it is not a fraction of a beat at all.
    if (division == SnapDivision::off)
        return 1;

    if (division == SnapDivision::bar)
        return perBeat * juce::jmax (1, beatsPerBar);

    const auto fraction = fractionOf (division);

    return juce::jmax (1, perBeat * fraction.numerator / fraction.denominator);
}

bool NoteTools::fitsGrid (SnapDivision division, int stepsPerBeat, int beatsPerBar) noexcept
{
    // The absence of a grid fits every grid.
    if (division == SnapDivision::off)
        return true;

    if (division == SnapDivision::bar)
        return juce::jmax (1, beatsPerBar) >= 1;

    const auto perBeat = juce::jmax (1, stepsPerBeat);
    const auto fraction = fractionOf (division);
    const auto cell = perBeat * fraction.numerator;

    // Exact, AND coarser than a step. A division that lands between steps
    // cannot be snapped to; one that IS a step is the identity, and offering it
    // is how Quantize came to look like it did nothing.
    return cell % fraction.denominator == 0 && cell / fraction.denominator > 1;
}

juce::String NoteTools::nameForSnap (SnapDivision division, int beatUnit)
{
    // The divisions are fractions OF A BEAT, so their names are the beat's note
    // value scaled by the division. In 4/4 that reproduces the old fixed
    // labels exactly; in 6/8 "quarter" correctly reads 1/8.
    const auto unit = juce::jmax (1, beatUnit);
    const auto noteValue = [unit] (int multiplier) { return juce::String (unit * multiplier); };

    // A triplet is three in the time of two, so it is named after the note it
    // subdivides with a T after it - "1/8 T" is three in the time of two
    // eighths, which is what every score and every other sequencer calls it.
    switch (division)
    {
        case SnapDivision::off: return "Off";
        case SnapDivision::thirtysecond: return "1/" + noteValue (8);
        case SnapDivision::sixteenthTriplet: return "1/" + noteValue (4) + " T";
        case SnapDivision::sixteenth: return "1/" + noteValue (4);
        case SnapDivision::eighthTriplet: return "1/" + noteValue (2) + " T";
        case SnapDivision::eighth: return "1/" + noteValue (2);
        case SnapDivision::quarter: return "1/" + noteValue (1);
        case SnapDivision::half:
            return unit >= 2 ? "1/" + juce::String (unit / 2) : juce::String ("2/1");
        case SnapDivision::bar: return "Bar";
    }

    return "1/" + noteValue (4);
}

juce::String NoteTools::nameForGrid (int stepsPerBeat, int beatUnit)
{
    // Named after what it BUYS - the finest note it can place - rather than
    // after its step count, which is a number with no musical meaning to
    // anybody choosing one.
    const auto triplets = placesOnGrid (SnapDivision::sixteenthTriplet, stepsPerBeat);

    for (const auto division :
         { SnapDivision::thirtysecond, SnapDivision::sixteenthTriplet, SnapDivision::sixteenth })
    {
        if (! placesOnGrid (division, stepsPerBeat))
            continue;

        const auto name = nameForSnap (division, beatUnit);

        // A grid that places triplets says so, unless the note it is named
        // after is already one.
        return triplets && division != SnapDivision::sixteenthTriplet ? name + " T" : name;
    }

    return nameForSnap (SnapDivision::sixteenth, beatUnit);
}

SnapDivision NoteTools::nearestFittingSnap (SnapDivision wanted, int stepsPerBeat,
                                            int beatsPerBar) noexcept
{
    if (fitsGrid (wanted, stepsPerBeat, beatsPerBar))
        return wanted;

    // The ladder runs finest to coarsest, so walking forward from `wanted` is
    // walking towards divisions this grid is more likely to hold.
    for (int i = indexOfSnap (wanted) + 1; i < numSnapDivisions; ++i)
        if (fitsGrid (allSnapDivisions[i], stepsPerBeat, beatsPerBar))
            return allSnapDivisions[i];

    return SnapDivision::bar;
}

SnapDivision NoteTools::snapFromIndex (int index) noexcept
{
    if (index < 0 || index >= numSnapDivisions)
        return SnapDivision::sixteenth;

    return allSnapDivisions[index];
}

int NoteTools::indexOfSnap (SnapDivision division) noexcept
{
    for (int i = 0; i < numSnapDivisions; ++i)
        if (allSnapDivisions[i] == division)
            return i;

    return 0;
}

int NoteTools::snapFloor (int step, int snapSteps) noexcept
{
    const auto snap = juce::jmax (1, snapSteps);
    return juce::jmax (0, step) / snap * snap;
}

int NoteTools::snapNearest (int step, int snapSteps) noexcept
{
    const auto snap = juce::jmax (1, snapSteps);
    return (juce::jmax (0, step) + snap / 2) / snap * snap;
}

int NoteTools::snapCeil (int step, int snapSteps) noexcept
{
    const auto snap = juce::jmax (1, snapSteps);
    return (juce::jmax (0, step) + snap - 1) / snap * snap;
}

juce::Array<juce::ValueTree> NoteTools::notesOnChannel (const juce::ValueTree& pattern,
                                                        int channelId)
{
    juce::Array<juce::ValueTree> notes;

    for (const auto& note : pattern)
        if (note.hasType (ids::NOTE) && (int) note[ids::ch] == channelId)
            notes.add (note);

    return notes;
}

juce::Array<juce::ValueTree> NoteTools::scopeFor (const juce::ValueTree& pattern, int channelId,
                                                  const juce::Array<juce::ValueTree>& selection)
{
    if (! selection.isEmpty())
        return selection;

    return notesOnChannel (pattern, channelId);
}

juce::ValueTree NoteTools::noteCovering (const juce::ValueTree& pattern, int channelId, int step,
                                         int pitch)
{
    for (const auto& note : pattern)
    {
        if (! note.hasType (ids::NOTE) || (int) note[ids::ch] != channelId
            || (int) note[ids::pitch] != pitch)
            continue;

        const auto start = (int) note[ids::step];
        const auto length = juce::jmax (1, (int) note[ids::lengthSteps]);

        if (step >= start && step < start + length)
            return note;
    }

    return {};
}

juce::ValueTree NoteTools::sliceNote (juce::ValueTree pattern, juce::ValueTree note, int atStep,
                                      juce::UndoManager* undo)
{
    if (! pattern.isValid() || ! note.isValid())
        return {};

    const auto start = (int) note[ids::step];
    const auto length = juce::jmax (1, (int) note[ids::lengthSteps]);

    if (atStep <= start || atStep >= start + length)
        return {};

    ProjectEdits::resizeNote (note, atStep - start, undo);

    // Both halves keep the original velocity. A slice is a rhythmic edit; making
    // the tail quieter would be a second edit nobody asked for.
    return ProjectEdits::addNote (pattern, (int) note[ids::ch], atStep, start + length - atStep,
                                  (int) note[ids::pitch], (float) (double) note[ids::velocity],
                                  undo);
}

int NoteTools::quantize (juce::ValueTree pattern, const juce::Array<juce::ValueTree>& notes,
                         int snapSteps, int stepsPerBar, juce::UndoManager* undo)
{
    if (! pattern.isValid())
        return 0;

    for (auto note : notes)
        ProjectEdits::moveNote (note, snapNearest ((int) note[ids::step], snapSteps),
                                (int) note[ids::pitch], undo);

    // Deduplicate afterwards rather than during: two notes only collide once
    // both have moved, and checking as we go would compare a moved note against
    // one still sitting at its original step.
    juce::Array<juce::ValueTree> duplicates;

    for (int i = 0; i < notes.size(); ++i)
    {
        auto kept = notes.getUnchecked (i);

        if (! kept.isValid() || duplicates.contains (kept))
            continue;

        for (int j = i + 1; j < notes.size(); ++j)
        {
            auto other = notes.getUnchecked (j);

            if (! other.isValid() || duplicates.contains (other))
                continue;

            if ((int) other[ids::ch] != (int) kept[ids::ch]
                || (int) other[ids::step] != (int) kept[ids::step]
                || (int) other[ids::pitch] != (int) kept[ids::pitch])
                continue;

            // Keep the longer of the two: the survivor should sound for as long
            // as the longest note that was there before.
            ProjectEdits::resizeNote (
                kept, juce::jmax ((int) kept[ids::lengthSteps], (int) other[ids::lengthSteps]),
                undo);
            duplicates.add (other);
        }
    }

    for (auto duplicate : duplicates)
        ProjectEdits::removeNote (pattern, duplicate, undo);

    // A note rounded up past the end is still a note, and a last bar emptied by
    // rounding down is a bar the pattern no longer needs.
    ProjectEdits::fitPatternToNotes (pattern, stepsPerBar, undo);

    return duplicates.size();
}

int NoteTools::transpose (const juce::Array<juce::ValueTree>& notes, int semitones, int minPitch,
                          int maxPitch, juce::UndoManager* undo)
{
    if (notes.isEmpty() || semitones == 0)
        return 0;

    // Clamped across the whole group before anything moves, so a chord keeps its
    // intervals when it reaches a limit instead of collapsing onto one pitch.
    auto allowed = semitones;

    for (const auto& note : notes)
    {
        const auto pitch = (int) note[ids::pitch];
        allowed = juce::jlimit (minPitch - pitch, maxPitch - pitch, allowed);
    }

    if (allowed == 0)
        return 0;

    for (auto note : notes)
        ProjectEdits::moveNote (note, (int) note[ids::step], (int) note[ids::pitch] + allowed,
                                undo);

    return allowed;
}

void NoteTools::randomize (juce::ValueTree pattern, const juce::Array<juce::ValueTree>& notes,
                           const RandomizeOptions& options, juce::Random& random, int stepsPerBar,
                           juce::UndoManager* undo)
{
    const auto movesVelocity = options.velocityAmount > 0.0;
    const auto movesStep = options.stepAmount > 0;

    if (! movesVelocity && ! movesStep)
        return;

    for (auto note : notes)
    {
        if (movesVelocity)
        {
            const auto offset = (random.nextDouble() * 2.0 - 1.0) * options.velocityAmount;

            // setNoteVelocity owns the 0.05 floor that keeps a quiet note
            // audible, so the clamp stays in exactly one place.
            ProjectEdits::setNoteVelocity (note, (double) note[ids::velocity] + offset, undo);
        }

        if (movesStep)
        {
            const auto offset = random.nextInt (2 * options.stepAmount + 1) - options.stepAmount;

            ProjectEdits::moveNote (note, (int) note[ids::step] + offset, (int) note[ids::pitch],
                                    undo);
        }
    }

    if (movesStep)
        ProjectEdits::fitPatternToNotes (pattern, stepsPerBar, undo);
}

} // namespace dew
