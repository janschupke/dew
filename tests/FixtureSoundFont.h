#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace dew::testing
{

/** Builds a SoundFont 2 file, byte by byte, in memory.

    A generated fixture rather than a committed `.sf2`, for two reasons. A real
    font is megabytes and would be the largest thing in the repository by an
    order of magnitude. And a builder can be asked for the file that is WRONG in
    one specific way - a truncated chunk, a sample id past the end, a loop
    outside its sample - which is what the reader's bounds checking has to be
    tested against and what no committed file could provide.

    It also doubles as documentation of the layout: every chunk the reader walks
    is written here in the order the format puts it.
*/
struct SoundFontBuilder
{
    /** One generator, as the file stores it: an operator and sixteen raw bits
        whose meaning the operator decides. */
    struct Generator
    {
        int oper = 0;
        int amount = 0;
    };

    struct Sample
    {
        juce::String name { "Sample" };
        std::vector<juce::int16> data;
        int rootKey = 60;
        int correctionCents = 0;
        int type = 1; ///< 1 mono, 2 right, 4 left; 0x8000 adds ROM
        juce::uint32 loopStart = 0;
        juce::uint32 loopEnd = 0;
        juce::uint32 sampleRate = 44100;
    };

    /** A zone of an instrument. The terminal `sampleID` generator is added by
        the writer, so a zone with no sample index is a global zone. */
    struct Zone
    {
        std::vector<Generator> generators;
        int sampleIndex = -1; ///< -1 makes this the instrument's global zone
    };

    struct Instrument
    {
        juce::String name { "Instrument" };
        std::vector<Zone> zones;
    };

    /** A zone of a preset. Its generators are OFFSETS, and the terminal
        `instrument` generator is added by the writer. */
    struct PresetZone
    {
        std::vector<Generator> generators;
        int instrumentIndex = -1; ///< -1 makes this the preset's global zone
    };

    struct Preset
    {
        juce::String name { "Preset" };
        int bank = 0;
        int program = 0;
        std::vector<PresetZone> zones;
    };

    std::vector<Sample> samples;
    std::vector<Instrument> instruments;
    std::vector<Preset> presets;

    /** Chunks to leave out entirely, by four-character id. For the tests that
        ask what a font missing its preset data does. */
    juce::StringArray omit;

    juce::MemoryBlock build() const;

    // --- ready-made fonts -----------------------------------------------------

    /** One preset, one instrument, one looped mono sample across every key. */
    static SoundFontBuilder minimal();

    /** The same, as a hard-panned left and right pair - what a fifth of the
        samples in a real library are. */
    static SoundFontBuilder stereo();

    /** A short rising ramp, so a test can tell which part of it was read. */
    static std::vector<juce::int16> rampSamples (int length);
};

} // namespace dew::testing
