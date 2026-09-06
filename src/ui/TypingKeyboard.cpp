#include "ui/TypingKeyboard.h"

#include "model/ProjectEdits.h"

namespace dew
{

TypingKeyboard::TypingKeyboard (ProjectDocument& d, AudioEngine& e, EditorState& s)
    : document (d)
    , engine (e)
    , editorState (s)
{
    juce::Desktop::getInstance().addFocusChangeListener (this);
}

TypingKeyboard::~TypingKeyboard()
{
    juce::Desktop::getInstance().removeFocusChangeListener (this);

    listenTo (nullptr);

    if (fallbackTarget != nullptr)
        fallbackTarget->removeKeyListener (this);
}

void TypingKeyboard::followFocus (juce::Component& fallback)
{
    fallbackTarget = &fallback;
    fallback.addKeyListener (this);

    // Whatever has the keyboard right now, so switching the mode on does not
    // have to wait for the next focus change to take effect.
    listenTo (juce::Component::getCurrentlyFocusedComponent());
}

void TypingKeyboard::listenTo (juce::Component* target)
{
    if (auto* previous = listening.get(); previous != nullptr && previous != fallbackTarget)
        previous->removeKeyListener (this);

    listening = target;

    // addKeyListener is addIfNotAlreadyThere, so landing on the fallback itself
    // costs nothing and never doubles up.
    if (target != nullptr)
        target->addKeyListener (this);
}

void TypingKeyboard::globalFocusChanged (juce::Component* focused)
{
    // Nothing focused means the window went away, and a key held across that
    // has no release coming.
    if (focused == nullptr)
        releaseAll();

    listenTo (focused);
}

bool TypingKeyboard::textHasTheKeyboard()
{
    if (auto* target = dynamic_cast<juce::TextInputTarget*> (
            juce::Component::getCurrentlyFocusedComponent()))
        return target->isTextInputActive();

    return false;
}

void TypingKeyboard::setEnabled (bool shouldPlay)
{
    if (enabled == shouldPlay)
        return;

    enabled = shouldPlay;

    if (! enabled)
        releaseAll();
}

void TypingKeyboard::setOctave (int wanted)
{
    const auto clamped = juce::jlimit (typingKeys::lowestOctave, typingKeys::highestOctave, wanted);

    if (clamped == octave)
        return;

    // Before the move, not after: the pitches that are sounding belong to the
    // old octave, and after the move nothing would know which they were.
    releaseAll();

    octave = clamped;

    if (onOctaveChanged != nullptr)
        onOctaveChanged (octave);
}

bool TypingKeyboard::handleKeyPress (const juce::KeyPress& key)
{
    if (! enabled || textHasTheKeyboard())
        return false;

    // BARE keys only. Shift is compared here even though keys::matches ignores
    // it, and that is the whole reason shift-R still opens Randomize and every
    // cmd- and alt- stroke in dew is untouched by this mode.
    if (key.getModifiers().withoutMouseButtons().getRawFlags() != 0)
        return false;

    if (keys::matches ({ typingKeys::octaveDownKey, 0 }, key))
    {
        setOctave (octave - 1);
        return true;
    }

    if (keys::matches ({ typingKeys::octaveUpKey, 0 }, key))
    {
        setOctave (octave + 1);
        return true;
    }

    const auto semitone = typingKeys::semitoneFor (key);

    if (semitone < 0)
        return false;

    // CONSUMED whether or not it sounds. A row that lands off the top of MIDI
    // is still a key this mode owns, and letting it fall through would mean the
    // top of the keyboard quietly started quantizing at the highest octave.
    refreshHeldKeys();
    return true;
}

bool TypingKeyboard::refreshHeldKeys()
{
    return refreshHeldKeys ([] (int keyCode)
                            { return juce::KeyPress::isKeyCurrentlyDown (keyCode); });
}

bool TypingKeyboard::refreshHeldKeys (const KeyStateSource& isDown)
{
    if (! enabled || textHasTheKeyboard() || isDown == nullptr)
    {
        releaseAll();
        return false;
    }

    std::bitset<128> wanted;

    for (const auto& note : typingKeys::table())
    {
        if (! isDown (note.keyCode))
            continue;

        if (const auto pitch = typingKeys::pitchFor (note.semitone, octave); pitch >= 0)
            wanted.set ((size_t) pitch);
    }

    for (int pitch = 0; pitch < 128; ++pitch)
    {
        const auto want = wanted.test ((size_t) pitch);
        const auto have = sounding.test ((size_t) pitch);

        if (want && ! have)
            soundNote (pitch);
        else if (have && ! want)
            silenceNote (pitch);
    }

    return sounding.any();
}

void TypingKeyboard::releaseAll()
{
    for (int pitch = 0; pitch < 128; ++pitch)
        if (sounding.test ((size_t) pitch))
            silenceNote (pitch);
}

void TypingKeyboard::soundNote (int pitch)
{
    // The channel is resolved at every note rather than cached, for the same
    // reason the piano roll's audition resolves it at every click: the
    // selection can move under a held key, and a stale index would play the
    // wrong instrument.
    const auto channelIndex = ProjectEdits::channelIndexForId (document.getState(),
                                                               editorState.getSelectedChannelId());

    if (channelIndex < 0)
        return;

    // Marked BEFORE the push, so a note the ring refuses still has a note-off
    // sent for it. previewNoteOff on a pitch that never sounded is harmless;
    // a sounding pitch nothing tracks is a note that hangs until the mode ends.
    sounding.set ((size_t) pitch);

    engine.previewNoteOn (channelIndex, pitch, (float) editorState.getLastNoteVelocity());
}

void TypingKeyboard::silenceNote (int pitch)
{
    sounding.reset ((size_t) pitch);

    const auto channelIndex = ProjectEdits::channelIndexForId (document.getState(),
                                                               editorState.getSelectedChannelId());

    if (channelIndex < 0)
        return;

    // Per pitch, never previewAllOff: the piano roll auditions through the same
    // ring, and taking a finger off a letter must not cut off a note somebody
    // is holding with the mouse.
    engine.previewNoteOff (channelIndex, pitch);
}

bool TypingKeyboard::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    return handleKeyPress (key);
}

bool TypingKeyboard::keyStateChanged (bool, juce::Component*)
{
    // State-based, so this is safe to reach twice in one dispatch - which it
    // will be whenever the focused component is not the fallback and declines
    // the key. Returning "something is sounding" stops the chain while notes
    // are held and lets it continue when none are, which is what keeps a
    // release from being swallowed by a component that wanted it.
    return enabled && refreshHeldKeys();
}

} // namespace dew
