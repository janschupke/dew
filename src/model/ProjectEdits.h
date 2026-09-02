#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace dew
{

/** Every mutation the editor can make to a project.

    UI components call these rather than touching the tree, for three reasons:
    the undo transaction is opened in one place, the semantics (what "toggle a
    step" means when a note is already there) live somewhere testable, and the
    step grid and the piano roll cannot drift apart - they edit the same notes
    through the same functions.

    Every function takes the UndoManager so that nothing bypasses undo.
*/
struct ProjectEdits
{
    // --- lookup --------------------------------------------------------------
    static juce::ValueTree findChannel (const juce::ValueTree& project, int channelId);
    static juce::ValueTree findPattern (const juce::ValueTree& project, int patternId);
    static juce::ValueTree findMixerTrack (const juce::ValueTree& project, int mixerTrackId);

    /** The note at exactly this channel/step/pitch, or an invalid tree. */
    static juce::ValueTree findNote (const juce::ValueTree& pattern, int channelId, int step, int pitch);

    /** Any note on this channel covering this step, whatever its pitch - what
        the step grid shows as a lit cell.
    */
    static juce::ValueTree findNoteAtStep (const juce::ValueTree& pattern, int channelId, int step);

    static int nextFreeId (const juce::ValueTree& project, const juce::Identifier& childType);

    // --- notes ---------------------------------------------------------------
    /** Step grid: lights a step, or clears it if already lit. Returns true if a
        note was added.
    */
    static bool toggleStep (juce::ValueTree pattern, int channelId, int step, int pitch,
                            juce::UndoManager*);

    static juce::ValueTree addNote (juce::ValueTree pattern, int channelId, int step,
                                    int lengthSteps, int pitch, float velocity,
                                    juce::UndoManager*);

    static void removeNote (juce::ValueTree pattern, juce::ValueTree note, juce::UndoManager*);

    static void moveNote (juce::ValueTree note, int newStep, int newPitch, juce::UndoManager*);

    static void resizeNote (juce::ValueTree note, int newLengthSteps, juce::UndoManager*);

    static void setNoteVelocity (juce::ValueTree note, double velocity, juce::UndoManager*);

    /** Grows a pattern so every note fits, and returns true if it had to.

        Never shrinks: a pattern deliberately left longer than its notes is a
        rest at the end, and silently trimming it would destroy that.
    */
    static bool growPatternToFitNotes (juce::ValueTree pattern, juce::UndoManager*);

    // --- channels ------------------------------------------------------------
    static juce::ValueTree addChannel (juce::ValueTree project, const juce::String& name,
                                       juce::UndoManager*);

    static void removeChannel (juce::ValueTree project, juce::ValueTree channel, juce::UndoManager*);

    // --- patterns ------------------------------------------------------------
    static juce::ValueTree addPattern (juce::ValueTree project, juce::UndoManager*);

    /** Deep-copies a pattern, notes and all, under a new id and name. Returns an
        invalid tree if the source is not a pattern of this project.
    */
    static juce::ValueTree duplicatePattern (juce::ValueTree project, juce::ValueTree pattern,
                                             juce::UndoManager*);

    /** Removes a pattern and every playlist clip that referred to it, as one undo
        step - a clip pointing at a missing pattern would be dropped by the next
        snapshot anyway, so removing it here keeps the document consistent.

        Refuses to remove the last pattern: a project with none has nothing to
        edit and nothing to play. Returns true if the pattern was removed.
    */
    static bool removePattern (juce::ValueTree project, juce::ValueTree pattern,
                               juce::UndoManager*);

    /** Steps needed to contain every note in this pattern, for "fit to notes".
        At least one step, so an empty pattern does not collapse to nothing.
    */
    static int lengthNeededForNotes (const juce::ValueTree& pattern);

    // --- effects -------------------------------------------------------------
    /** Appends an effect of this type to a channel or mixer track's chain.

        Returns an invalid tree if the chain is already full: a chain longer
        than the engine renders would look like an effect that stopped working.
    */
    static juce::ValueTree addEffect (juce::ValueTree project, juce::ValueTree owner,
                                      const juce::String& type, juce::UndoManager*);

    static void removeEffect (juce::ValueTree owner, juce::ValueTree effect, juce::UndoManager*);

    /** Reorders a chain. Position is counted among effects only, so the
        instrument child a channel also carries cannot shift the result.
    */
    static void moveEffect (juce::ValueTree owner, juce::ValueTree effect, int newPosition,
                            juce::UndoManager*);

    static int countEffects (const juce::ValueTree& owner);

    /** Every node in the project that can carry an effect chain. */
    static juce::Array<juce::ValueTree> effectChainOwners (const juce::ValueTree& project);

    // --- playlist ------------------------------------------------------------
    static juce::ValueTree addClip (juce::ValueTree playlistTrack, int patternId, int startBar,
                                    int lengthBars, juce::UndoManager*);

    static void removeClip (juce::ValueTree playlistTrack, juce::ValueTree clip, juce::UndoManager*);

    static void moveClip (juce::ValueTree clip, int newStartBar, juce::UndoManager*);

    /** Moves a clip to another track, and to a new bar, as one undo step.

        Returns the clip that ends up on the target track. A ValueTree cannot be
        in two parents at once, so this removes and re-adds rather than
        reparenting, and the caller must use the returned tree afterwards - the
        original is detached.
    */
    static juce::ValueTree moveClipToTrack (juce::ValueTree fromTrack, juce::ValueTree clip,
                                            juce::ValueTree toTrack, int newStartBar,
                                            juce::UndoManager*);

    static void resizeClip (juce::ValueTree clip, int newLengthBars, juce::UndoManager*);

    /** Bars needed to contain every clip on every track. */
    static int barsNeededForClips (const juce::ValueTree& project);

    /** Grows the song so every clip fits, and returns true if it had to.
        Never shrinks, for the same reason a pattern never shrinks: trailing
        empty bars are a deliberate silence.
    */
    static bool growSongToFitClips (juce::ValueTree project, juce::UndoManager*);

    /** The clip covering this bar on this track, or an invalid tree. */
    static juce::ValueTree findClipAtBar (const juce::ValueTree& playlistTrack, int bar);
};

} // namespace dew
