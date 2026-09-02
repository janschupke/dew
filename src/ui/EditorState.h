#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace dew
{

/** What the editor is looking at, as opposed to what the project contains.

    Deliberately not in the ValueTree: which channel is selected is not part of
    the document, should not be saved, and should not go on the undo stack.

    It also remembers the shape of the last note the user placed. Without that,
    every note drawn in the piano roll reverts to one step at full velocity, and
    writing a passage of held or quiet notes means re-editing each one.
*/
class EditorState : public juce::ChangeBroadcaster
{
public:
    int getSelectedChannelId() const noexcept  { return selectedChannelId; }
    int getCurrentPatternId() const noexcept   { return currentPatternId; }

    void setSelectedChannelId (int id)
    {
        if (std::exchange (selectedChannelId, id) != id)
            sendChangeMessage();
    }

    void setCurrentPatternId (int id)
    {
        if (std::exchange (currentPatternId, id) != id)
            sendChangeMessage();
    }

    // --- last placed note ----------------------------------------------------
    int getLastNoteLengthSteps() const noexcept { return lastNoteLengthSteps; }
    double getLastNoteVelocity() const noexcept { return lastNoteVelocity; }

    /** Records the shape of a note the user just drew, so the next one matches. */
    void rememberNote (int lengthSteps, double velocity)
    {
        lastNoteLengthSteps = juce::jmax (1, lengthSteps);
        lastNoteVelocity = juce::jlimit (0.05, 1.0, velocity);
    }

private:
    int selectedChannelId = 1;
    int currentPatternId = 1;

    int lastNoteLengthSteps = 1;
    double lastNoteVelocity = 1.0;
};

} // namespace dew
