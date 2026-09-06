#pragma once

#include <utility>

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
    int getSelectedChannelId() const noexcept
    {
        return selectedChannelId;
    }
    int getCurrentPatternId() const noexcept
    {
        return currentPatternId;
    }
    int getSelectedMixerTrackId() const noexcept
    {
        return selectedMixerTrackId;
    }

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

    // --- recording -----------------------------------------------------------
    /** Which audio channel a take will be recorded into, or 0 for none.

        Session state like every other selection here: arming is about what the
        next take does, not about the project, and it must not make a document
        dirty or land on the undo stack. Only one channel can be armed - dew
        records one input, and two armed channels would have to mean one of them
        silently loses.
    */
    int getArmedChannelId() const noexcept
    {
        return armedChannelId;
    }

    void setArmedChannelId (int id)
    {
        if (std::exchange (armedChannelId, id) != id)
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

    // --- instrument ----------------------------------------------------------
    /** Whether the instrument's own controls are showing.

        The same kind of state an effect card's fold is, for the same reasons:
        per-session, off the undo stack, and not a change to the project. The
        instrument band is the tallest thing in the sidebar, and it was the one
        band there that could not be got out of the way to see the chain below
        it - the effect cards had folded since they were written.

        A single flag rather than one per channel: the panel shows one channel
        at a time, and "I am working on the effects" is a statement about what
        the person is doing, not about which channel they last clicked.
    */
    bool isInstrumentExpanded() const noexcept
    {
        return instrumentExpanded;
    }

    void setInstrumentExpanded (bool shouldBeExpanded)
    {
        if (std::exchange (instrumentExpanded, shouldBeExpanded) == shouldBeExpanded)
            return;

        sendChangeMessage();
    }

    // --- oscillators ---------------------------------------------------------
    /** Which oscillator slot the instrument panel is editing.

        View state, for the same reasons the expanded effect cards are: it is
        not part of the project, has no business on the undo stack, and must not
        make a document dirty. Not remembered per channel - the panel shows one
        channel at a time, and a per-channel slot would make the header jump
        about as you click through the rack.

        Clamped by the panel rather than here, so this header does not have to
        know how many oscillators the schema declares.
    */
    int getSelectedOscillator() const noexcept
    {
        return selectedOscillator;
    }

    void setSelectedOscillator (int index)
    {
        if (std::exchange (selectedOscillator, index) != index)
            sendChangeMessage();
    }

    // --- the selected span of the arrangement --------------------------------
    /** Which bars are selected in the playlist, half-open and 0-based.

        View state, like everything else here: a selection is not part of the
        document, must not make a project dirty, and must not land on the undo
        stack. An empty range means nothing is selected, which is also what a
        click on the ruler leaves behind.
    */
    juce::Range<int> getSelectedBarRange() const noexcept
    {
        return selectedBars;
    }
    bool hasBarSelection() const noexcept
    {
        return ! selectedBars.isEmpty();
    }

    void setSelectedBarRange (juce::Range<int> range)
    {
        const auto clamped = range.getLength() > 0
                                 ? juce::Range<int> (juce::jmax (0, range.getStart()),
                                                     juce::jmax (1, range.getEnd()))
                                 : juce::Range<int>();

        if (std::exchange (selectedBars, clamped) != clamped)
            sendChangeMessage();
    }

    void clearBarSelection()
    {
        setSelectedBarRange ({});
    }

    // --- the selected span of a pattern --------------------------------------
    /** Which steps are selected in the piano roll, half-open and 0-based.

        A second range rather than a unit conversion on the one above. The
        playlist selects BARS of the arrangement and the piano roll selects STEPS
        of a pattern: different units, different scopes, and the two are audible
        in different transport modes. Folding them into one would mean a span
        picked out in the roll silently moving when the mode changed.
    */
    juce::Range<int> getSelectedStepRange() const noexcept
    {
        return selectedSteps;
    }
    bool hasStepSelection() const noexcept
    {
        return ! selectedSteps.isEmpty();
    }

    void setSelectedStepRange (juce::Range<int> range)
    {
        const auto clamped = range.getLength() > 0
                                 ? juce::Range<int> (juce::jmax (0, range.getStart()),
                                                     juce::jmax (1, range.getEnd()))
                                 : juce::Range<int>();

        if (std::exchange (selectedSteps, clamped) != clamped)
            sendChangeMessage();
    }

    void clearStepSelection()
    {
        setSelectedStepRange ({});
    }

    // --- last placed note ----------------------------------------------------
    int getLastNoteLengthSteps() const noexcept
    {
        return lastNoteLengthSteps;
    }
    double getLastNoteVelocity() const noexcept
    {
        return lastNoteVelocity;
    }

    /** Records the shape of a note the user just drew, so the next one matches. */
    void rememberNote (int lengthSteps, double velocity)
    {
        lastNoteLengthSteps = juce::jmax (1, lengthSteps);
        lastNoteVelocity = juce::jlimit (0.05, 1.0, velocity);
    }

    /** The same idea one level up: the length of the last clip sized, so the
        playlist's paint tool lays a run of clips at the length being worked in
        rather than one bar at a time.
    */
    int getLastClipLengthBars() const noexcept
    {
        return lastClipLengthBars;
    }

    void rememberClip (int lengthBars)
    {
        lastClipLengthBars = juce::jmax (1, lengthBars);
    }

private:
    int selectedChannelId = 1;
    int currentPatternId = 1;
    int selectedMixerTrackId = 1;
    int selectedOscillator = 0;
    int armedChannelId = 0;

    int lastNoteLengthSteps = 1;
    double lastNoteVelocity = 1.0;
    int lastClipLengthBars = 1;

    juce::Range<int> selectedBars;
    juce::Range<int> selectedSteps;

    juce::Array<int> expandedEffects;

    /** Open, because that is what the panel has always been. */
    bool instrumentExpanded = true;
};

} // namespace dew
