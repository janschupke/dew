#include "PianoRollComponent.h"

#include "../model/Ids.h"
#include "../model/ProjectEdits.h"
#include "DewLookAndFeel.h"

namespace dew
{

namespace
{

bool isBlackKey (int pitch)
{
    switch (pitch % 12)
    {
        case 1: case 3: case 6: case 8: case 10: return true;
        default: return false;
    }
}

juce::String noteName (int pitch)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[pitch % 12]) + juce::String (pitch / 12 - 1);
}

} // namespace

PianoRollComponent::PianoRollComponent (ProjectDocument& d, AudioEngine& e, EditorState& s)
    : document (d), engine (e), editorState (s)
{
    setComponentID ("pianoRoll");
    document.getState().addListener (this);
    editorState.addChangeListener (this);
    setSize (900, numRows * rowHeight);
    startTimerHz (30);
}

PianoRollComponent::~PianoRollComponent()
{
    document.getState().removeListener (this);
    editorState.removeChangeListener (this);
}

void PianoRollComponent::refresh()
{
    document.getState().addListener (this);
    repaint();
}

juce::ValueTree PianoRollComponent::currentPattern() const
{
    return ProjectEdits::findPattern (document.getState(), editorState.getCurrentPatternId());
}

int PianoRollComponent::numSteps() const
{
    const auto pattern = currentPattern();
    return pattern.isValid() ? juce::jmax (1, (int) pattern[ids::lengthSteps]) : 16;
}

float PianoRollComponent::stepWidth() const
{
    return (float) juce::jmax (1, getWidth() - keyboardWidth) / (float) numSteps();
}

int PianoRollComponent::stepAtX (int x) const
{
    return juce::jlimit (0, numSteps() - 1, (int) ((float) (x - keyboardWidth) / stepWidth()));
}

int PianoRollComponent::pitchAtY (int y) const
{
    return juce::jlimit (lowestPitch, highestPitch, highestPitch - y / rowHeight);
}

juce::Rectangle<float> PianoRollComponent::boundsForNote (const juce::ValueTree& note) const
{
    const auto step = (int) note[ids::step];
    const auto length = juce::jmax (1, (int) note[ids::lengthSteps]);
    const auto pitch = (int) note[ids::pitch];

    return { (float) keyboardWidth + (float) step * stepWidth(),
             (float) ((highestPitch - pitch) * rowHeight),
             (float) length * stepWidth(),
             (float) rowHeight };
}

juce::ValueTree PianoRollComponent::noteAt (juce::Point<int> position) const
{
    const auto pattern = currentPattern();
    const auto channelId = editorState.getSelectedChannelId();

    for (const auto& note : pattern)
        if (note.hasType (ids::NOTE) && (int) note[ids::ch] == channelId
            && boundsForNote (note).contains (position.toFloat()))
            return note;

    return {};
}

bool PianoRollComponent::isOnRightEdge (const juce::ValueTree& note, juce::Point<int> position) const
{
    const auto bounds = boundsForNote (note);
    const auto edge = juce::jmin (8.0f, bounds.getWidth() * 0.35f);

    return (float) position.x >= bounds.getRight() - edge;
}

void PianoRollComponent::mouseMove (const juce::MouseEvent& event)
{
    const auto note = noteAt (event.getPosition());

    setMouseCursor (note.isValid() && isOnRightEdge (note, event.getPosition())
                        ? juce::MouseCursor::LeftRightResizeCursor
                        : juce::MouseCursor::NormalCursor);
}

void PianoRollComponent::mouseDown (const juce::MouseEvent& event)
{
    auto pattern = currentPattern();

    if (! pattern.isValid() || event.x < keyboardWidth)
        return;

    auto& undo = document.getUndoManager();
    auto note = noteAt (event.getPosition());

    // Right-click or alt-click deletes, which is the FL convention.
    if (event.mods.isPopupMenu() || event.mods.isAltDown())
    {
        if (note.isValid())
        {
            undo.beginNewTransaction ("Delete note");
            ProjectEdits::removeNote (pattern, note, &undo);
            repaint();
        }
        return;
    }

    if (note.isValid())
    {
        draggedNote = note;

        if (isOnRightEdge (note, event.getPosition()))
        {
            gesture = Gesture::resizing;
            undo.beginNewTransaction ("Resize note");
        }
        else
        {
            gesture = Gesture::moving;
            dragStepOffset = stepAtX (event.x) - (int) note[ids::step];
            dragPitchOffset = pitchAtY (event.y) - (int) note[ids::pitch];
            undo.beginNewTransaction ("Move note");
        }

        return;
    }

    // Empty space: add a note there and let the same drag resize it, so a note
    // can be drawn at a length in one gesture.
    undo.beginNewTransaction ("Add note");

    draggedNote = ProjectEdits::addNote (pattern, editorState.getSelectedChannelId(),
                                         stepAtX (event.x), 1, pitchAtY (event.y), 1.0f, &undo);
    gesture = Gesture::resizing;
    repaint();
}

void PianoRollComponent::mouseDrag (const juce::MouseEvent& event)
{
    if (! draggedNote.isValid())
        return;

    auto& undo = document.getUndoManager();

    if (gesture == Gesture::resizing)
    {
        const auto length = stepAtX (event.x) - (int) draggedNote[ids::step] + 1;
        ProjectEdits::resizeNote (draggedNote, juce::jmax (1, length), &undo);
    }
    else if (gesture == Gesture::moving)
    {
        ProjectEdits::moveNote (draggedNote,
                                juce::jlimit (0, numSteps() - 1, stepAtX (event.x) - dragStepOffset),
                                pitchAtY (event.y) - dragPitchOffset,
                                &undo);
    }

    repaint();
}

void PianoRollComponent::mouseUp (const juce::MouseEvent&)
{
    draggedNote = {};
    gesture = Gesture::none;
}

void PianoRollComponent::timerCallback()
{
    const auto step = (int) engine.getPlayheadSteps();

    if (step != lastPlayheadStep)
    {
        lastPlayheadStep = step;
        repaint();
    }
}

void PianoRollComponent::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) { repaint(); }
void PianoRollComponent::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&)             { repaint(); }
void PianoRollComponent::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int)      { repaint(); }
void PianoRollComponent::changeListenerCallback (juce::ChangeBroadcaster*)                    { repaint(); }

void PianoRollComponent::resized()
{
    // Height is fixed by the pitch range; the viewport scrolls it.
    setSize (getWidth(), numRows * rowHeight);
}

void PianoRollComponent::paint (juce::Graphics& g)
{
    g.fillAll (Palette::panelDark);

    const auto steps = numSteps();
    const auto width = stepWidth();
    const auto stepsPerBeat = juce::jmax (1, (int) document.getState()[ids::stepsPerBeat]);
    const auto channel = ProjectEdits::findChannel (document.getState(), editorState.getSelectedChannelId());

    // --- rows ----------------------------------------------------------------
    for (int pitch = lowestPitch; pitch <= highestPitch; ++pitch)
    {
        const auto y = (float) ((highestPitch - pitch) * rowHeight);
        const auto rowArea = juce::Rectangle<float> (0.0f, y, (float) getWidth(), (float) rowHeight);

        if (isBlackKey (pitch))
        {
            g.setColour (Palette::background);
            g.fillRect (rowArea.withLeft ((float) keyboardWidth));
        }

        // Keyboard gutter.
        g.setColour (isBlackKey (pitch) ? Palette::background : Palette::panel);
        g.fillRect (rowArea.withWidth ((float) keyboardWidth));

        if (pitch % 12 == 0)
        {
            g.setColour (Palette::textDim);
            g.setFont (juce::FontOptions (9.0f));
            g.drawText (noteName (pitch), 2, (int) y, keyboardWidth - 4, rowHeight,
                        juce::Justification::centredLeft);

            g.setColour (Palette::lineStrong);
            g.drawHorizontalLine ((int) y, (float) keyboardWidth, (float) getWidth());
        }
        else
        {
            g.setColour (Palette::line.withAlpha (0.4f));
            g.drawHorizontalLine ((int) y, (float) keyboardWidth, (float) getWidth());
        }
    }

    // --- columns -------------------------------------------------------------
    for (int step = 0; step <= steps; ++step)
    {
        const auto x = (float) keyboardWidth + (float) step * width;
        g.setColour (step % stepsPerBeat == 0 ? Palette::lineStrong : Palette::line.withAlpha (0.5f));
        g.drawVerticalLine ((int) x, 0.0f, (float) getHeight());
    }

    // --- notes ---------------------------------------------------------------
    const auto pattern = currentPattern();
    const auto channelId = editorState.getSelectedChannelId();
    const auto colour = channel.isValid()
                          ? juce::Colour::fromString ("ff" + channel[ids::colour].toString().getLastCharacters (6))
                          : Palette::accent;

    for (const auto& note : pattern)
    {
        if (! note.hasType (ids::NOTE))
            continue;

        const auto bounds = boundsForNote (note).reduced (1.0f, 1.0f);
        const auto isSelectedChannel = (int) note[ids::ch] == channelId;

        if (isSelectedChannel)
        {
            g.setColour (colour.withAlpha (0.85f));
            g.fillRoundedRectangle (bounds, 2.0f);
            g.setColour (colour.brighter (0.4f));
            g.drawRoundedRectangle (bounds, 2.0f, 1.0f);
        }
        else
        {
            // Other channels' notes are visible but obviously not editable here.
            g.setColour (Palette::lineStrong.withAlpha (0.4f));
            g.fillRoundedRectangle (bounds, 2.0f);
        }
    }

    // --- playhead ------------------------------------------------------------
    if (engine.isPlaying() && engine.getMode() == Transport::Mode::pattern)
    {
        const auto step = (int) engine.getPlayheadSteps() % steps;
        g.setColour (Palette::playhead.withAlpha (0.25f));
        g.fillRect (juce::Rectangle<float> ((float) keyboardWidth + (float) step * width, 0.0f,
                                            width, (float) getHeight()));
    }

    if (! channel.isValid())
    {
        g.setColour (Palette::textDim);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ("Select a channel in the Channel Rack", getLocalBounds(),
                    juce::Justification::centred);
    }
}

} // namespace dew
