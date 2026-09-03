#include "io/MidiRouter.h"

namespace dew
{

namespace
{
constexpr int ccModWheel = 1;
constexpr int ccSustainPedal = 64;
constexpr int ccAllSoundOff = 120;
constexpr int ccAllNotesOff = 123;

/** The wheel's centre. 14 bits, so 0..16383 with 8192 at rest. */
constexpr int pitchWheelCentre = 8192;
} // namespace

MidiRouter::MidiRouter (AudioEngine& e)
    : engine (e)
{
}

int MidiRouter::packNote (int pitch, int velocity, int midiChannel) noexcept
{
    return (juce::jlimit (0, 127, pitch) & 0xff) | ((juce::jlimit (0, 127, velocity) & 0xff) << 8)
           | ((juce::jlimit (0, 16, midiChannel) & 0xff) << 16);
}

void MidiRouter::setTargetChannel (int channelIndex) noexcept
{
    const auto previous = targetChannel.exchange (channelIndex, std::memory_order_relaxed);

    if (previous == channelIndex)
        return;

    // Release what the old channel was holding and unbend it. Without this a
    // note keeps sounding on a channel nothing is playing any more, and the
    // bend it was left at never returns to rest.
    //
    // Through the message thread's path: setTargetChannel is called from the
    // editor, not from a MIDI callback.
    engine.previewAllOff();
    engine.setChannelBend (previous, 0.0f);
    engine.setChannelModulation (previous, 0.0f);

    clearRequested.store (true, std::memory_order_release);
}

void MidiRouter::setChannelFilter (int midiChannel) noexcept
{
    channelFilter.store (juce::jlimit (omni, 16, midiChannel), std::memory_order_relaxed);
}

void MidiRouter::setTranspose (int semitones) noexcept
{
    transpose.store (juce::jlimit (-maxTranspose, maxTranspose, semitones),
                     std::memory_order_relaxed);
}

void MidiRouter::panic() noexcept
{
    engine.midiAllOff();

    const auto channel = targetChannel.load (std::memory_order_relaxed);
    engine.setChannelBend (channel, 0.0f);
    engine.setChannelModulation (channel, 0.0f);

    sounding.reset();
    heldBySustain.reset();
    sustainDown = false;
}

void MidiRouter::reset() noexcept
{
    engine.previewAllOff();

    const auto channel = targetChannel.load (std::memory_order_relaxed);
    engine.setChannelBend (channel, 0.0f);
    engine.setChannelModulation (channel, 0.0f);

    clearRequested.store (true, std::memory_order_release);
}

bool MidiRouter::transposed (int pitch, int& out) const noexcept
{
    const auto shifted = pitch + transpose.load (std::memory_order_relaxed);

    if (shifted < 0 || shifted > 127)
        return false;

    out = shifted;
    return true;
}

void MidiRouter::noteOn (int pitch, float velocity) noexcept
{
    // A key pressed again while the pedal holds it must not be left in the
    // sustained set, or lifting the pedal would kill the new note.
    heldBySustain.reset ((size_t) pitch);
    sounding.set ((size_t) pitch);

    engine.midiNoteOn (targetChannel.load (std::memory_order_relaxed), pitch, velocity);
}

void MidiRouter::noteOff (int pitch) noexcept
{
    if (sustainDown)
    {
        // The key is up but the pedal is down: remember it, and release it when
        // the pedal lifts.
        if (sounding.test ((size_t) pitch))
            heldBySustain.set ((size_t) pitch);

        return;
    }

    sounding.reset ((size_t) pitch);
    engine.midiNoteOff (targetChannel.load (std::memory_order_relaxed), pitch);
}

void MidiRouter::handleMessage (const juce::MidiMessage& message) noexcept
{
    // The message thread asked for a clear. Doing it here rather than there is
    // what keeps this bookkeeping single-threaded.
    if (clearRequested.exchange (false, std::memory_order_acquire))
    {
        sounding.reset();
        heldBySustain.reset();
        sustainDown = false;
    }

    const auto filter = channelFilter.load (std::memory_order_relaxed);

    if (filter != omni && message.getChannel() != filter)
        return;

    const auto channel = targetChannel.load (std::memory_order_relaxed);

    if (message.isNoteOnOrOff())
    {
        int pitch = 0;

        if (! transposed (message.getNoteNumber(), pitch))
            return;

        activity.fetch_add (1, std::memory_order_relaxed);

        // A note-on with velocity 0 is a note-off. Every controller sends them,
        // and treating them as note-ons leaves every key ringing forever.
        // juce::MidiMessage::isNoteOn() already applies this rule.
        if (message.isNoteOn())
        {
            const auto velocity = message.getFloatVelocity();
            lastNote.store (packNote (pitch, message.getVelocity(), message.getChannel()),
                            std::memory_order_relaxed);
            noteOn (pitch, velocity);
        }
        else
        {
            noteOff (pitch);
        }

        return;
    }

    if (message.isPitchWheel())
    {
        activity.fetch_add (1, std::memory_order_relaxed);

        const auto offset = message.getPitchWheelValue() - pitchWheelCentre;
        const auto semitones = (float) offset / (float) pitchWheelCentre * bendRangeSemitones;

        engine.setChannelBend (channel, semitones);
        return;
    }

    if (message.isController())
    {
        const auto number = message.getControllerNumber();
        const auto value = message.getControllerValue();

        activity.fetch_add (1, std::memory_order_relaxed);

        if (number == ccModWheel)
        {
            engine.setChannelModulation (channel, (float) value / 127.0f);
            return;
        }

        if (number == ccSustainPedal)
        {
            const auto down = value >= sustainThreshold;

            if (down == sustainDown)
                return;

            sustainDown = down;

            if (! down)
            {
                // Pedal up: everything whose key was already released goes now.
                for (size_t pitch = 0; pitch < heldBySustain.size(); ++pitch)
                {
                    if (! heldBySustain.test (pitch))
                        continue;

                    sounding.reset (pitch);
                    engine.midiNoteOff (channel, (int) pitch);
                }

                heldBySustain.reset();
            }

            return;
        }

        if (number == ccAllNotesOff || number == ccAllSoundOff)
        {
            panic();
            return;
        }
    }

    // Everything else - clock, aftertouch, program change, sysex - is not
    // something dew plays, and is dropped rather than guessed at.
}

} // namespace dew
