#pragma once

#include <juce_core/juce_core.h>

#include "engine/SoundFont.h"

namespace dew
{

/** Reads a SoundFont 2 file into the flat region table the engine plays.

    Hand-written rather than taken from a library, and the reason is the same
    one that keeps this repository at two dependencies: a soundfont player from
    outside is a whole second synth engine, with its own envelopes, its own
    filter and its own voice stealing, none of whose parameters would be
    ParamSpec rows. What is actually needed is a READER, and the format's
    sampler core is a bounded thing.

    What is read, and what is not, was decided by surveying a 446-font library
    rather than by reading the specification cover to cover:

      - modulators (pmod/imod)          4 files of 446 use them at all
      - the two LFOs and their targets  ~1.2% of zones route one anywhere
      - modEnvToPitch                   2 zones
      - chorus and reverb sends         dew has its own effect chain
      - the 24-bit sm24 extension       no file in the library carries one
      - ROM samples                     no consumer font references them

    The modulation envelope IS read, because modEnvToFilterFc alone is 173 zones
    - more than every LFO route put together - and it is what gives those fonts
    their filter sweeps.

    EVERY offset in the file is treated as hostile. This parses something a
    person chose off their disk, so each chunk length, bag index, sample id and
    loop point is checked against what was actually read. A malformed font
    yields warnings and no presets; it never reads out of bounds, never asserts
    and never throws.
*/
struct SoundFontFile
{
    struct Result
    {
        SoundFontData font;

        /** What was wrong, or what was deliberately ignored. Empty on a clean
            read of a font using nothing this reader cuts. */
        juce::StringArray warnings;

        bool isValid() const noexcept
        {
            return font.isValid();
        }
    };

    /** Reads a whole font. Message thread - it reads the file and allocates. */
    static Result read (const juce::File&);

    /** The same, from bytes already in hand. What the tests drive, and what
        makes the reader testable without a fixture on disk. */
    static Result parse (const void* data, size_t sizeInBytes, const juce::String& name);

    /** Whether a path names a soundfont, case-insensitively. Two files in a
        real library are spelled `.SF2`, and an exact compare hides them. */
    static bool isSoundFont (const juce::File&);
};

} // namespace dew
