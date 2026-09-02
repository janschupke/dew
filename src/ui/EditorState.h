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
    int getSelectedChannelId() const noexcept    { return selectedChannelId; }
    int getCurrentPatternId() const noexcept     { return currentPatternId; }
    int getSelectedMixerTrackId() const noexcept { return selectedMixerTrackId; }

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

    /** Which mixer strip the effect chain editor is pointed at. */
    void setSelectedMixerTrackId (int id)
    {
        if (std::exchange (selectedMixerTrackId, id) != id)
            sendChangeMessage();
    }

    // --- effect chain -------------------------------------------------------
    /** Whether an effect's parameters are showing.

        View state, not document state: which cards are open is per-session, has
        no business on the undo stack, and must not make a project dirty. Keyed
        on the effect's id rather than its position, so reordering a chain does
        not shuffle which cards are open.
    */
    bool isEffectExpanded (int effectId) const
    {
        return expandedEffects.contains (effectId);
    }

    void setEffectExpanded (int effectId, bool shouldBeExpanded)
    {
        const auto changed = shouldBeExpanded ? ! expandedEffects.contains (effectId)
                                              : expandedEffects.contains (effectId);

        if (! changed)
            return;

        if (shouldBeExpanded)
            expandedEffects.add (effectId);
        else
            expandedEffects.removeAllInstancesOf (effectId);

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
    int selectedMixerTrackId = 1;

    int lastNoteLengthSteps = 1;
    double lastNoteVelocity = 1.0;

    juce::Array<int> expandedEffects;
};

} // namespace dew
