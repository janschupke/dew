#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <bitset>
#include <functional>

#include "app/ProjectDocument.h"
#include "engine/AudioEngine.h"
#include "ui/EditorState.h"
#include "ui/TypingKeys.h"

namespace dew
{

/** The letter keys as an instrument.

    A MODE, not a binding. While it is on, a bare key from typingKeys::table()
    plays the selected channel and reaches nothing else - not the piano roll's
    `q`, not the tools on `1` `2` `3`, not the application's bare `r`. Anything
    with a modifier passes straight through, so Space still plays, cmd-Z still
    undoes and shift-R still opens Randomize. It is silent whenever a text field
    has the keyboard, because typing into the score tab has to type.

    Playback only. Notes go through AudioEngine's PREVIEW ring, which is the
    same path a click on a piano-roll key takes: nothing is written to the
    document, nothing dirties a project and nothing lands on the undo stack.
    (Not the MIDI ring - PreviewQueue is single-producer by construction and its
    producer is the message thread, which is where this runs.)

    ## Why a KeyListener that follows the focus

    ComponentPeer::handleKeyPress walks from the focused component upward and,
    at each level, calls that component's KEY LISTENERS BEFORE its own
    keyPressed. So a listener on MainComponent beats the command manager's
    mapping set on the window - which is how bare `r` is shadowed - but it does
    NOT beat the piano roll, which handles `q` itself. Attaching to whatever
    currently holds the keyboard is the one rule that always wins, and it costs
    no edit to any editor: no seam through EditorTabs, no `if (typing)` at the
    top of six keyPressed overrides.

    ## Why note-off is a poll rather than an event

    JUCE only reports a key RELEASE through keyStateChanged, which says "some
    key changed" and nothing about which. So refreshHeldKeys re-reads every row
    with KeyPress::isKeyCurrentlyDown and sounds the difference. That is JUCE's
    own answer in MidiKeyboardComponent, and it buys two things: the call is
    idempotent, so being reached twice in one chain is harmless; and two keys
    that are the same note - `,` and `q` are both the octave - do not silence
    each other when one of them is let go.
*/
class TypingKeyboard : public juce::KeyListener, private juce::FocusChangeListener
{
public:
    TypingKeyboard (ProjectDocument&, AudioEngine&, EditorState&);
    ~TypingKeyboard() override;

    /** Turns the mode on or off. Everything sounding is released on the way
        off, so a key held as the mode ends does not hang. */
    void setEnabled (bool);

    bool isEnabled() const noexcept
    {
        return enabled;
    }

    int getOctave() const noexcept
    {
        return octave;
    }

    /** Moves the whole map. Releases first: the pitches under the fingers are
        about to be different ones, and the notes already sounding have no
        note-off ahead of them otherwise. */
    void setOctave (int);

    /** Answers one press, or says it is not ours.

        The decision, callable directly: grabKeyboardFocus does nothing without
        a ComponentPeer, so a test can never make this arrive as a real key.
    */
    bool handleKeyPress (const juce::KeyPress&);

    /** Which keys are physically down.

        A PARAMETER rather than a call straight into juce::KeyPress, for the
        same reason focusGroups::nextFocusFor takes the focused component: there
        is no ComponentPeer in a headless test, so nothing can press a key and
        isKeyCurrentlyDown answers false forever. The no-argument overload below
        is the real keyboard and is what everything but a test calls.
    */
    using KeyStateSource = std::function<bool (int keyCode)>;

    /** Re-reads which mapped keys are down and sounds the difference.

        Idempotent. Returns whether it ACTED - whether this call started or
        stopped a note - and not whether anything is sounding, because the
        answer is what keyStateChanged consumes the event on. A poll that
        changed nothing is a poll about somebody else's key.

        `modifiers` applies the same BARE-key rule handleKeyPress applies, and
        has to: KeyPress::isKeyCurrentlyDown matches a letter case-insensitively
        and knows nothing about what is held with it, so without this cmd-Z
        sounds semitone 0 underneath the undo it is performing. The overload
        without it means "nothing held", which is what every caller that has
        already checked wants.
    */
    bool refreshHeldKeys (const KeyStateSource&, const juce::ModifierKeys&);
    bool refreshHeldKeys (const KeyStateSource&);
    bool refreshHeldKeys();

    /** Silences everything this has started, and nothing it has not. */
    void releaseAll();

    /** How many notes this is holding. For the tests, which cannot listen to an
        engine. */
    int getSoundingCount() const noexcept
    {
        return (int) sounding.count();
    }

    /** Fired when the octave moves, so the host can say so in the status bar. */
    std::function<void (int octave)> onOctaveChanged;

    // --- juce::KeyListener ---------------------------------------------------
    bool keyPressed (const juce::KeyPress&, juce::Component*) override;
    bool keyStateChanged (bool isKeyDown, juce::Component*) override;

    /** Listens to `fallback` for good, and to whatever holds the keyboard as it
        moves. `fallback` catches everything that bubbles - a key pressed with
        nothing focused, and every key the focused component declined. */
    void followFocus (juce::Component& fallback);

private:
    void globalFocusChanged (juce::Component*) override;

    /** Whether a text field is taking the keyboard, in which case this is
        silent. One rule covers the score editor, a number field mid-edit and
        the preferences search box. */
    static bool textHasTheKeyboard();

    void listenTo (juce::Component*);

    void soundNote (int pitch);
    void silenceNote (int pitch);

    ProjectDocument& document;
    AudioEngine& engine;
    EditorState& editorState;

    bool enabled = false;
    int octave = typingKeys::defaultOctave;

    /** Which pitches THIS has started. A set rather than a count per key,
        because the map has two keys for the same note and what matters is
        whether any of them is down. */
    std::bitset<128> sounding;

    juce::WeakReference<juce::Component> listening;
    juce::Component* fallbackTarget = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TypingKeyboard)
};

} // namespace dew
