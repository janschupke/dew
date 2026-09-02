#include "model/NoteTools.h"

#include "model/Ids.h"
#include "model/ProjectEdits.h"

namespace dew
{

const SnapDivision NoteTools::allSnapDivisions[NoteTools::numSnapDivisions] = {
    SnapDivision::sixteenth,
    SnapDivision::eighth,
    SnapDivision::quarter,
    SnapDivision::half,
    SnapDivision::bar
};

int NoteTools::stepsForSnap (SnapDivision division, int stepsPerBeat, int beatsPerBar) noexcept
{
    const auto perBeat = juce::jmax (1, stepsPerBeat);
    const auto perBar = perBeat * juce::jmax (1, beatsPerBar);

    switch (division)
    {
        case SnapDivision::sixteenth: return juce::jmax (1, perBeat / 4);
        case SnapDivision::eighth:    return juce::jmax (1, perBeat / 2);
        case SnapDivision::quarter:   return perBeat;
        case SnapDivision::half:      return perBeat * 2;
        case SnapDivision::bar:       return perBar;
    }

    return 1;
}

juce::String NoteTools::nameForSnap (SnapDivision division, int beatUnit)
{
    // The divisions are fractions OF A BEAT, so their names are the beat's note
    // value scaled by the division. In 4/4 that reproduces the old fixed
    // labels exactly; in 6/8 "quarter" correctly reads 1/8.
    const auto unit = juce::jmax (1, beatUnit);
    const auto noteValue = [unit] (int multiplier) { return juce::String (unit * multiplier); };

    switch (division)
    {
        case SnapDivision::sixteenth: return "1/" + noteValue (4);
        case SnapDivision::eighth:    return "1/" + noteValue (2);
        case SnapDivision::quarter:   return "1/" + noteValue (1);
        case SnapDivision::half:      return unit >= 2 ? "1/" + juce::String (unit / 2)
                                                       : juce::String ("2/1");
        case SnapDivision::bar:       return "Bar";
    }

    return "1/" + noteValue (4);
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

juce::Array<juce::ValueTree> NoteTools::notesOnChannel (const juce::ValueTree& pattern, int channelId)
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

juce::ValueTree NoteTools::noteCovering (const juce::ValueTree& pattern, int channelId,
                                         int step, int pitch)
{
    for (const auto& note : pattern)
    {
        if (! note.hasType (ids::NOTE) || (int) note[ids::ch] != channelId
            || (int) note[ids::pitch] != pitch)
            continue;

        const auto start  = (int) note[ids::step];
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

    const auto start  = (int) note[ids::step];
    const auto length = juce::jmax (1, (int) note[ids::lengthSteps]);

    if (atStep <= start || atStep >= start + length)
        return {};

    ProjectEdits::resizeNote (note, atStep - start, undo);

    // Both halves keep the original velocity. A slice is a rhythmic edit; making
    // the tail quieter would be a second edit nobody asked for.
    return ProjectEdits::addNote (pattern, (int) note[ids::ch], atStep,
                                  start + length - atStep, (int) note[ids::pitch],
                                  (float) (double) note[ids::velocity], undo);
}

int NoteTools::quantize (juce::ValueTree pattern, const juce::Array<juce::ValueTree>& notes,
                         int snapSteps, juce::UndoManager* undo)
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
            ProjectEdits::resizeNote (kept, juce::jmax ((int) kept[ids::lengthSteps],
                                                        (int) other[ids::lengthSteps]), undo);
            duplicates.add (other);
        }
    }

    for (auto duplicate : duplicates)
        ProjectEdits::removeNote (pattern, duplicate, undo);

    // A note rounded up past the end is still a note: grow rather than lose it.
    ProjectEdits::growPatternToFitNotes (pattern, undo);

    return duplicates.size();
}

int NoteTools::transpose (const juce::Array<juce::ValueTree>& notes, int semitones,
                          int minPitch, int maxPitch, juce::UndoManager* undo)
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
        ProjectEdits::moveNote (note, (int) note[ids::step], (int) note[ids::pitch] + allowed, undo);

    return allowed;
}

void NoteTools::randomize (juce::ValueTree pattern, const juce::Array<juce::ValueTree>& notes,
                           const RandomizeOptions& options, juce::Random& random,
                           juce::UndoManager* undo)
{
    const auto movesVelocity = options.velocityAmount > 0.0;
    const auto movesStep     = options.stepAmount > 0;

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

            ProjectEdits::moveNote (note, (int) note[ids::step] + offset,
                                    (int) note[ids::pitch], undo);
        }
    }

    if (movesStep)
        ProjectEdits::growPatternToFitNotes (pattern, undo);
}

} // namespace dew
