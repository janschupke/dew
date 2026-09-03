#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include "model/AutomationCurve.h"
#include "model/AutomationTargets.h"

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

    /** The channel's position among the project's channels, or -1.

        The engine and the snapshot index channels by position, while the editor
        knows a channel by id. Anything crossing that boundary - auditioning a
        key, pointing MIDI input at a channel - has to resolve it the same way
        the snapshot builder does rather than assume the two agree.
    */
    static int channelIndexForId (const juce::ValueTree& project, int channelId);

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

    // --- properties ----------------------------------------------------------
    /** Writes one property, opening the undo transaction the write belongs to.

        ProjectEdits had no scalar setter at all, so all twenty-seven property
        writes in src/ui went straight to ValueTree and each re-implemented the
        transaction rule around it:

            if (! dragging)
                undo.beginNewTransaction (name);

        copy-pasted five times. The rule matters because a drag emits a value
        per frame: without it, dragging a knob across its range makes a hundred
        undo steps and getting back to where you started means pressing undo a
        hundred times. SampleSection had no such guard AT ALL - dragging an
        audio channel's fade or transpose did exactly that, a regression of a
        fix the README claims is done.

        @param continuingTransaction  do not open a transaction; join the one
                                      already open. True for every value after
                                      the first in one drag, and for a write
                                      that is part of a larger action - a clip
                                      repointed at the pattern that was just
                                      duplicated for it belongs to that action,
                                      not to one of its own.
    */
    static void setProperty (juce::ValueTree node, const juce::Identifier& property,
                             const juce::var& value, juce::UndoManager*,
                             const juce::String& transactionName,
                             bool continuingTransaction = false);

    /** Grows a pattern so every note fits, and returns true if it had to.

        Never shrinks: a pattern deliberately left longer than its notes is a
        rest at the end, and silently trimming it would destroy that.
    */
    static bool growPatternToFitNotes (juce::ValueTree pattern, juce::UndoManager*);

    // --- channels ------------------------------------------------------------
    static juce::ValueTree addChannel (juce::ValueTree project, const juce::String& name,
                                       juce::UndoManager*);

    /** A channel that plays a recorded or imported sample instead of its
        oscillators. Identical to addChannel in every other respect - the id,
        the mixer routing and the canonical child order are the same problem.
    */
    static juce::ValueTree addAudioChannel (juce::ValueTree project, const juce::String& name,
                                            juce::UndoManager*);

    static bool isAudioChannel (const juce::ValueTree& channel);

    /** Points an audio channel at a file, and records what was found in it.

        The length is stored because the editor has to lay a clip out before the
        audio has been read, and because a missing file should still draw as the
        right width rather than collapsing to nothing.
    */
    static void setSampleSource (juce::ValueTree channel, const juce::String& relativeOrAbsolutePath,
                                 int sourceSampleRate, int lengthSamples, juce::UndoManager*);

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

    // --- oscillators ---------------------------------------------------------
    /** A channel's nth oscillator slot, or an invalid tree.

        Named rather than open-coded because the panel, the factory and the
        tests all need "the second oscillator of this channel", and
        getChildWithName (ids::OSC) - which every one of them used to say -
        silently means "the first" now that a channel carries several.
    */
    static juce::ValueTree oscillatorAt (const juce::ValueTree& channel, int index);

    static int countOscillators (const juce::ValueTree& channel);

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

    // --- automation ----------------------------------------------------------
    /** Creates an automation definition pointed at a curated target, with two
        points so a fresh clip is a line rather than an empty box.
    */
    static juce::ValueTree addAutomation (juce::ValueTree project, const AutomationTarget&,
                                          juce::UndoManager*);

    static juce::ValueTree findAutomation (const juce::ValueTree& project, int automationId);

    /** Creates the automation AND places a clip for it, as ONE undo step.

        In the model rather than in the playlist, because eleven controls can ask
        for this now and the arrangement is not on screen for most of them.

        The clip lands on the first lane with room at `startBar`, so a new curve
        is visible rather than stacked invisibly under a pattern clip. If every
        lane is occupied there it ADDS a track rather than giving up: that was
        rare from one button and is routine once every knob offers it, and a
        menu item that silently does nothing is worse than one more lane.
    */
    static juce::ValueTree addAutomationWithClip (juce::ValueTree project, const AutomationTarget&,
                                                  int startBar, int lengthBars,
                                                  juce::UndoManager*);

    /** Removes an automation and every clip that referred to it. */
    static bool removeAutomation (juce::ValueTree project, juce::ValueTree automation,
                                  juce::UndoManager*);

    /** Adds a point, keeping the list sorted by step. An existing point at the
        same step is moved rather than duplicated - two points on one step is a
        curve with no defined value there.
    */
    static juce::ValueTree addAutomationPoint (juce::ValueTree automation, double step,
                                               double value, juce::UndoManager*);

    /** Moves a point, CLAMPED between its neighbours.

        A drag cannot carry a point past the one before or after it. The old
        behaviour was to let it through and then re-sort the tree, which meant a
        drag silently reordered the children under the point being dragged and
        put a moveChild on the undo stack for every frame of it.

        The clamp leaves a gap rather than stopping exactly ON the neighbour:
        addAutomationPoint treats two approximately-equal steps as one point, so
        landing on a neighbour would make a pair the rest of the model regards as
        single, and a zero-width segment whose bend means nothing.
    */
    static void moveAutomationPoint (juce::ValueTree automation, juce::ValueTree point,
                                     double step, double value, juce::UndoManager*);

    /** The smallest gap between two automation points, in steps.

        Not zero, for the reason above. Small enough to be invisible - a bar is
        120px at the maximum zoom, so a sixty-fourth of a step is a tenth of a
        pixel - and far larger than the epsilon approximatelyEqual uses.

        Here rather than in Tokens.h: it is a rule about what a document may
        contain, not a design value.
    */
    static constexpr double minPointGap = 1.0 / 64.0;

    static void removeAutomationPoint (juce::ValueTree automation, juce::ValueTree point,
                                       juce::UndoManager*);

    /** An automation's POINT children in step order.

        Public because the editor needs the same order the evaluator does, and
        was building its own array to get it - two walks of the same children
        with two chances to disagree about which of them count.
    */
    static juce::Array<juce::ValueTree> sortedAutomationPoints (const juce::ValueTree& automation);

    /** Sets the shape of the segment to this point's RIGHT, and nothing else.

        A stepped segment IGNORES the bend rather than losing it, so switching to
        step and back returns the curve you had. That is what makes the editor's
        three shape items reversible.
    */
    static void setPointShape (juce::ValueTree point, SegmentShape, juce::UndoManager*);

    /** What the editor's "Line" means: shape `curve`, bend zero, one undo step.

        Two shapes are stored and three are offered, because a line IS a curve
        with no bend - storing "line" as well would be a second place holding the
        same fact as `curve == 0`, free to disagree with the bend beside it. The
        menu ticks Line when the shape is curve and the bend is zero, which is
        derived rather than duplicated.
    */
    static void setPointStraight (juce::ValueTree point, juce::UndoManager*);

    /** Sets the bend of that same segment, clamped to -1..1.

        Named rather than a raw setProperty because every other automation
        mutation here is named and clamps, and because a source gate forbids an
        editor writing an undoable property by hand.
    */
    static void setPointCurve (juce::ValueTree point, double bend, juce::UndoManager*);

    /** Value of the curve at a step, 0..1. Linear between points, held flat
        before the first and after the last.
    */
    static double automationValueAt (const juce::ValueTree& automation, double step);

    // --- playlist ------------------------------------------------------------
    /** Appends a track to the arrangement.

        Playlist tracks carry no id - they are positional, unlike channels and
        mixer inserts - so there is nothing to allocate and nothing to renumber.
    */
    static juce::ValueTree addPlaylistTrack (juce::ValueTree project, const juce::String& name,
                                             juce::UndoManager*);

    /** Removes a track and the clips on it, as one undo step.

        The clips are its children, so they go with it. Automations are
        deliberately left behind: an AUTOMATION node is a reusable definition the
        "+ Automation" menu can place again, unlike the orphaned notes
        removeChannel cleans up, which are unreachable once their channel is gone.
    */
    static void removePlaylistTrack (juce::ValueTree project, juce::ValueTree track,
                                     juce::UndoManager*);

    static juce::ValueTree addClip (juce::ValueTree playlistTrack, int patternId, int startBar,
                                    int lengthBars, juce::UndoManager*);

    /** A clip that drives an automation curve rather than playing a pattern. */
    static juce::ValueTree addAutomationClip (juce::ValueTree playlistTrack, int automationId,
                                              int startBar, int lengthBars, juce::UndoManager*);

    /** A clip that plays an audio channel's sample. */
    static juce::ValueTree addAudioClip (juce::ValueTree playlistTrack, int channelId,
                                         int startBar, int lengthBars, juce::UndoManager*);

    static bool isAutomationClip (const juce::ValueTree& clip);

    static bool isAudioClip (const juce::ValueTree& clip);

    /** Neither of the other two. Defined by exclusion on purpose: a version 3
        file has clips with no `kind` at all, and those are notes.
    */
    static bool isMidiClip (const juce::ValueTree& clip);

    static void removeClip (juce::ValueTree playlistTrack, juce::ValueTree clip, juce::UndoManager*);

    static void moveClip (juce::ValueTree clip, int newStartBar, juce::UndoManager*);

    /** Moves a clip to another track, and to a new bar, as one undo step.

        Returns the clip that ends up on the target track. A ValueTree cannot be
        in two parents at once, so this removes and re-adds rather than
        reparenting, and the caller must use the returned tree afterwards - the
        original is detached.
    */
    /** Copies a clip onto a track, leaving the original where it is.

        Whole-node, so it carries whatever kind the clip is - MIDI, audio or
        automation - rather than needing to know the clip-kind table the way
        the three addXClip functions do. This is moveClipToTrack minus its
        removal, which is precisely what a copy-drag wants.
    */
    static juce::ValueTree copyClip (juce::ValueTree targetTrack, const juce::ValueTree& clip,
                                     int startBar, juce::UndoManager*);

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

    /** Sets the project's meter, rescaling the arrangement so it keeps sounding
        the same.

        A clip is stored in BARS, and the sequencer windows it as
        `startBar * stepsPerBar` to `+ lengthBars * stepsPerBar`. So redefining
        a bar moves every clip boundary: left alone, a one-bar clip of a
        sixteen-step pattern would span twelve steps in 3/4 and the pattern's
        last four steps would simply stop sounding. Every clip is therefore
        rescaled by `oldStepsPerBar / newStepsPerBar`, which holds its absolute
        position in steps, and `barsInSong` with them.

        The rescale is exact only when the ratio divides evenly - 16 to 12 is
        4/3, so a clip at bar 4 wants bar 5.33 and has to round. `wasExact`, if
        given, reports whether every clip landed on a whole bar, so the caller
        can say so rather than let it be discovered.

        Does nothing at all when the meter is unchanged, so this is safe to call
        from a combo box that re-selects the value it already had.
    */
    static void setMeter (juce::ValueTree project, int beatsPerBar, int beatUnit,
                          juce::UndoManager*, bool* wasExact = nullptr);

    // --- the score -----------------------------------------------------------
    /** Stores the arrangement language's source text in the project.

        Splits into one LINE per line, which is what makes a score readable in
        a diff. Writing this does NOT compile anything: the editor saves as you
        type and compiles when you ask, because a compile writes notes and
        nobody wants a pause in their typing to become an undo step full of
        them.
    */
    static void setScoreSource (juce::ValueTree project, const juce::String& text,
                                const juce::String& sourceName, juce::UndoManager*);

    /** The stored source, rejoined. Round-trips setScoreSource exactly,
        trailing newline included.
    */
    static juce::String scoreSource (const juce::ValueTree& project);

    static juce::String scoreSourceName (const juce::ValueTree& project);
};

} // namespace dew
