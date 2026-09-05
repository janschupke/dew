// =============================================================================
// The velocity lane: where a bar is, which one the pointer is on, and what a
// drag across them writes.
//
// A third translation unit of the same class, and the split PianoRollGestures
// reached first when it passed the length gate. It is also the honest seam: the
// lane is a second editor over the same notes, with its own geometry, and the
// only thing it shares with the note grid is the timeline.
//
// The two halves that must not drift are here together on purpose.
// velocityBarBounds draws a bar and applyVelocityAt reads a pointer back into a
// value, against the same floor and the same span - they disagreed by 8px once,
// and the bar top never sat under the cursor dragging it.
// =============================================================================

#include "ui/PianoRollComponent.h"

#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "ui/design/Tokens.h"

namespace dew
{

using namespace tokens;

juce::Rectangle<float> PianoRollComponent::velocityBarBounds (const juce::ValueTree& note) const
{
    const auto area = velocityArea();
    const auto velocity = (float) juce::jlimit (0.0, 1.0, (double) note[ids::velocity]);
    const auto barWidth = (float) juce::jlimit (3.0, 14.0, timeline.pixelsPerStep * 0.7);
    const auto x = (float) size::gutterKeyboard
                   + timeline.xForStep ((double) (int) note[ids::step]);

    const auto floor = (float) area.getBottom() - (float) barPadding;
    const auto height = velocity * (float) juce::jmax (1, area.getHeight() - barPadding * 2);

    return { x + 1.0f, floor - height, barWidth, height };
}

juce::ValueTree PianoRollComponent::velocityBarAt (juce::Point<int> position) const
{
    const auto channelId = editorState.getSelectedChannelId();

    for (const auto& note : currentPattern())
    {
        if (! note.hasType (ids::NOTE) || (int) note[ids::ch] != channelId)
            continue;

        // Generous vertically: the bar is a few pixels wide and its top is what
        // you aim at, so the whole column counts as a grab.
        const auto bar = velocityBarBounds (note);
        const auto column = juce::Rectangle<float> (
            bar.getX() - 2.0f, (float) velocityArea().getY(), bar.getWidth() + 4.0f,
            (float) velocityArea().getHeight());

        if (column.contains (position.toFloat()))
            return note;
    }

    return {};
}

void PianoRollComponent::applyVelocityAt (juce::Point<int> position)
{
    const auto lane = velocityArea();

    // Mapped against the same geometry velocityBarBounds draws with. These
    // disagreed by 8px, so the bar top never sat under the cursor dragging it.
    const auto floor = lane.getBottom() - barPadding;
    const auto span = juce::jmax (1, lane.getHeight() - barPadding * 2);
    const auto value = juce::jlimit (0.0, 1.0, (double) (floor - position.y) / (double) span);

    auto& undo = document.getUndoManager();

    if (draggedVelocityNote.isValid())
    {
        ProjectEdits::setNoteVelocity (draggedVelocityNote, value, &undo);
        repaint (lane);
        return;
    }

    const auto channelId = editorState.getSelectedChannelId();
    const auto step = stepAtX (position.x);

    // Only notes that start under the pointer, so dragging across the lane
    // paints a velocity curve without also hitting every held note under it.
    for (auto note : currentPattern())
        if (note.hasType (ids::NOTE) && (int) note[ids::ch] == channelId
            && (int) note[ids::step] == step && (selection.isEmpty() || isSelected (note)))
            ProjectEdits::setNoteVelocity (note, value, &undo);

    repaint (lane);
}

// --- notifications -----------------------------------------------------------

} // namespace dew
