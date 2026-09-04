#pragma once

#include <memory>

#include "engine/SoundFont.h"

namespace dew
{

/** Where a snapshot gets the soundfont a channel refers to.

    The twin of SampleProvider, and it exists for the same reason: buildSnapshot
    has to turn a stored path into playable data, and the thing that reads files
    belongs with the rest of the code that touches the OS. Without this
    interface the engine would depend on the io layer, and "the engine opens no
    files and no devices" - which a source gate enforces - would stop being true.

    Message thread only. Implementations may read from disk, and reading a
    soundfont is not cheap: a general MIDI font is a hundred megabytes.
*/
struct SoundFontProvider
{
    virtual ~SoundFontProvider() = default;

    /** The font a SOUNDFONT node's stored path refers to, or null if it cannot
        be read. Paths are stored relative to the project when the file sits
        beside it, so resolving one is the provider's job - a ValueTree does not
        know where its own file is. */
    virtual std::shared_ptr<const SoundFontData> soundFontFor (const juce::String& storedPath) = 0;
};

} // namespace dew
