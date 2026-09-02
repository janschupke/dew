#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "OfflineRenderer.h"

namespace dew
{

/** What to put in a MIDI file.

    A deliberately smaller question than RenderOptions asks. Sample rate, bit
    depth, tails, normalize, dither and fades all describe audio, and none of
    them mean anything here.
*/
struct MidiExportOptions
{
    Transport::Mode mode = Transport::Mode::song;
    int patternId = 1;

    /** Empty means the whole thing. Notes are rebased so the range starts at 0. */
    BarRange barRange;

    /** Format 1, one track per channel - which is what makes a DAW import each
        dew channel onto its own instrument track. Format 0 flattens everything
        into one track, for tools that read nothing else.
    */
    bool oneTrackPerChannel = true;

    /** By default a muted channel exports nothing, so the file matches what an
        audio render of the same scope would sound like.
    */
    bool includeMutedChannels = false;
};

/** Writes a project's notes as a standard MIDI file.

    Built from an EngineSnapshot rather than the raw tree, so it inherits the
    sequencer's own idea of what plays: resolved channel indices, playlist track
    audibility, and clip repeat. The parity test drives Sequencer::collect over
    the same span and requires the same notes to come out, because an exporter
    that quietly disagrees with the engine is worse than no exporter.
*/
struct MidiExporter
{
    static RenderReport writeToFile (const juce::ValueTree& project,
                                     const juce::File& destination,
                                     const MidiExportOptions& options = {});

    /** Exposed so a test can round-trip through memory without touching a disk. */
    static juce::MidiFile build (const juce::ValueTree& project,
                                 const MidiExportOptions& options,
                                 juce::StringArray& warnings,
                                 juce::int64& numNotes);

    /** Ticks per quarter note for a given grid.

        Derived rather than fixed at 960, because stepsPerBeat runs to 16 and 960
        does not divide by 7, 9, 11, 13 or 14 - which would round note starts off
        the grid, silently. No constant works for all of them either: the lowest
        common multiple of 1..16 is 720720, and MidiFile stores the time format
        in a short. This yields the conventional 960 for every common grid and an
        exact, in-range value for the rest.
    */
    static int ticksPerQuarterNoteFor (int stepsPerBeat) noexcept;

    /** MIDI channel 1..16 for a channel index, skipping 10.

        Ten is where General MIDI puts percussion, and a DAW that sees a track on
        it will happily load a drum kit over whatever the channel actually was.
    */
    static int midiChannelFor (int channelIndex) noexcept;
};

} // namespace dew
