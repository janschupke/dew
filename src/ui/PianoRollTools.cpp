// =============================================================================
// The piano roll's note tools, and what they act on.
//
// The same class, a second translation unit - the shape PianoRollPaint.cpp
// already uses.
//
// Quantize, transpose, randomize, paint and slice, plus the selection they act
// through. What makes them one file is the rule they all obey: everything here
// acts on THE SELECTION IF THERE IS ONE, AND THE WHOLE CHANNEL OTHERWISE, which
// is editScope() below and is the piano roll's single most load-bearing
// sentence. Six commands enforcing it separately is six chances to differ.
//
// The arithmetic itself is not here. Snapping, quantizing and slicing are
// model/NoteTools.h, which has no GUI and its own tests; this is where a
// gesture is turned into a call on it and wrapped in one undo transaction.
// =============================================================================

#include "i18n/Strings.h"
#include "ui/PianoRollComponent.h"

#include "model/Ids.h"
#include "model/Meter.h"
#include "model/NoteTools.h"
#include "model/ProjectEdits.h"
#include "ui/PianoRollNotes.h"
#include "ui/RandomizePanel.h"
#include "ui/design/Tokens.h"

namespace dew
{

using namespace tokens;
using namespace pianoRoll;

int PianoRollComponent::snapSteps() const
{
    const auto meter = Meter::of (document.getState());
    return NoteTools::stepsForSnap (toolbar.getSnap(), meter.stepsPerBeat, meter.beatsPerBar);
}

juce::Array<juce::ValueTree> PianoRollComponent::editScope() const
{
    return NoteTools::scopeFor (currentPattern(), editorState.getSelectedChannelId(), selection);
}

void PianoRollComponent::quantizeScope()
{
    auto pattern = currentPattern();
    const auto scope = editScope();

    if (! pattern.isValid() || scope.isEmpty())
        return;

    auto& undo = document.getUndoManager();
    undo.beginNewTransaction ("Quantize");
    NoteTools::quantize (pattern, scope, snapSteps(), stepsPerBar(), &undo);
    repaint();
}

void PianoRollComponent::transposeScope (int semitones)
{
    const auto scope = editScope();

    if (scope.isEmpty())
        return;

    auto& undo = document.getUndoManager();

    // Opened only once the group is known to be able to move, so a chord held
    // against the top of the keyboard does not fill the undo stack with edits
    // that changed nothing.
    undo.beginNewTransaction (std::abs (semitones) >= semitonesPerOctave ? "Transpose octave"
                                                                         : "Transpose");

    if (NoteTools::transpose (scope, semitones, lowestPitch, highestPitch, &undo) != 0)
        repaint();
}

void PianoRollComponent::openRandomizeDialog()
{
    const auto scope = editScope();

    if (scope.isEmpty())
        return;

    const auto scopeText = selection.isEmpty() ? "Applies to all " + juce::String (scope.size())
                                                     + " notes on this channel"
                                               : "Applies to the " + juce::String (scope.size())
                                                     + " selected notes";

    RandomizePanel::show (randomizeOptions, scopeText, this,
                          [this] (const NoteTools::RandomizeOptions& options)
                          {
                              randomizeOptions = options;

                              auto pattern = currentPattern();

                              // Re-resolved rather than captured: the dialog is
                              // asynchronous, and the notes it was opened over
                              // may not all still be there.
                              const auto notes = editScope();

                              if (! pattern.isValid() || notes.isEmpty())
                                  return;

                              auto& undo = document.getUndoManager();
                              undo.beginNewTransaction ("Randomize");
                              NoteTools::randomize (pattern, notes, options, random, stepsPerBar(),
                                                    &undo);
                              repaint();
                          });
}

bool PianoRollComponent::paintNoteAt (juce::Point<int> position)
{
    auto pattern = currentPattern();

    if (! pattern.isValid() || ! noteArea().contains (position))
        return false;

    const auto snap = snapSteps();
    const auto step = NoteTools::snapFloor (stepAtX (position.x), snap);
    const auto pitch = pitchAtY (position.y);

    if (step == lastPaintedCell.x && pitch == lastPaintedCell.y)
        return false;

    lastPaintedCell = { step, pitch };

    const auto channelId = editorState.getSelectedChannelId();

    // Anything already sounding at this pitch here, whether it starts in this
    // cell or runs through it - painting over a held note should not stack a
    // second one inside it.
    if (NoteTools::noteCovering (pattern, channelId, step, pitch).isValid())
        return false;

    auto& undo = document.getUndoManager();

    const auto length = juce::jmax (
        snap, NoteTools::snapCeil (editorState.getLastNoteLengthSteps(), snap));

    auto note = ProjectEdits::addNote (pattern, channelId, step, length, pitch,
                                       (float) editorState.getLastNoteVelocity(), &undo);
    selection.add (note);
    fitPatternLength (undo);
    return true;
}

void PianoRollComponent::sliceAlong (juce::Point<int> from, juce::Point<int> to)
{
    auto pattern = currentPattern();

    if (! pattern.isValid() || from.y == to.y)
        return; // a horizontal sweep crosses no row's centre, so it cuts nothing

    const auto channelId = editorState.getSelectedChannelId();
    const auto topY = (float) juce::jmin (from.y, to.y);
    const auto bottomY = (float) juce::jmax (from.y, to.y);

    juce::Array<juce::ValueTree> victims;
    juce::Array<int> cuts;

    for (const auto& note : pattern)
    {
        if (! note.hasType (ids::NOTE) || (int) note[ids::ch] != channelId)
            continue;

        const auto bounds = boundsForNote (note);
        const auto centreY = bounds.getCentreY();

        // Strictly spanned, so a drag that merely touches a row's edge does not
        // cut the note in it.
        if (centreY <= topY || centreY >= bottomY)
            continue;

        // Where the line is when it crosses this row.
        const auto t = ((double) centreY - from.y) / ((double) to.y - from.y);
        const auto crossingX = (double) from.x + t * ((double) to.x - from.x);

        if (crossingX < bounds.getX() || crossingX > bounds.getRight())
            continue;

        const auto cut = stepAtX ((int) crossingX);
        const auto start = (int) note[ids::step];
        const auto length = juce::jmax (1, (int) note[ids::lengthSteps]);

        if (cut <= start || cut >= start + length)
            continue;

        victims.add (note);
        cuts.add (cut);
    }

    if (victims.isEmpty())
        return;

    auto& undo = document.getUndoManager();
    undo.beginNewTransaction (victims.size() == 1 ? "Slice note" : "Slice notes");

    // The fragments become the selection: after cutting a run, the next thing
    // you do is almost always to move or delete one side of it.
    selection.clearQuick();

    for (int i = 0; i < victims.size(); ++i)
    {
        auto head = victims.getUnchecked (i);
        auto tail = NoteTools::sliceNote (pattern, head, cuts.getUnchecked (i), &undo);

        if (tail.isValid())
        {
            selection.add (head);
            selection.add (tail);
        }
    }

    repaint();
}

// --- selection ---------------------------------------------------------------

bool PianoRollComponent::isSelected (const juce::ValueTree& note) const
{
    return selection.contains (note);
}

void PianoRollComponent::selectOnly (const juce::ValueTree& note)
{
    selection.clearQuick();

    if (note.isValid())
        selection.add (note);
}

void PianoRollComponent::selectAllOnChannel()
{
    selection.clearQuick();

    const auto channelId = editorState.getSelectedChannelId();

    for (const auto& note : currentPattern())
        if (note.hasType (ids::NOTE) && (int) note[ids::ch] == channelId)
            selection.add (note);

    repaint();
}

void PianoRollComponent::deleteSelection()
{
    if (selection.isEmpty())
        return;

    auto pattern = currentPattern();
    auto& undo = document.getUndoManager();

    undo.beginNewTransaction (selection.size() == 1
                                  ? "Delete note"
                                  : "Delete " + juce::String (selection.size()) + " notes");

    // Removing a note calls back into valueTreeChildRemoved, which drops it from
    // `selection` - iterating the live array skipped every other note and left
    // most of a multi-note selection behind.
    const auto doomed = selection;
    selection.clearQuick();

    for (const auto& note : doomed)
        ProjectEdits::removeNote (pattern, note, &undo);

    fitPatternLength (undo);
    repaint();
}

// --- scrolling ---------------------------------------------------------------

} // namespace dew
