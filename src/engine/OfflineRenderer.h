#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include "AudioEngine.h"

namespace dew
{

/** How to render. At namespace scope rather than nested in OfflineRenderer:
    a nested type's default member initializers are not usable in a default
    argument inside the enclosing class definition.
*/
struct RenderOptions
{
    double sampleRate = 44100.0;
    int blockSize = 512;
    int bitDepth = 24;

    /** 0 means "as long as the material": the arrangement in song mode, or one
        pattern in pattern mode, played ONCE, then `tailSeconds` of release.

        An explicit value renders exactly that long and lets the material loop,
        which is what asking for "four seconds of this pattern" means.
    */
    double seconds = 0.0;

    Transport::Mode mode = Transport::Mode::song;
    int patternId = 1;

    /** Extra time rendered after the material ends, so release tails are not
        cut off mid-decay.
    */
    double tailSeconds = 1.0;
};

struct RenderReport
{
    juce::Result result = juce::Result::ok();
    double seconds = 0.0;
    juce::int64 numSamples = 0;
    float peak = 0.0f;
    float rms = 0.0f;
    juce::StringArray warnings;

    bool ok() const { return result.wasOk(); }
};

/** Renders a project to audio without an audio device.

    This is how playback gets verified: in CI, in tests, and on any machine
    without working sound. It drives the same AudioEngine the live path does, so
    a passing render is evidence about the real engine and not about a
    simplified stand-in.
*/
struct OfflineRenderer
{
    /** Renders into memory. */
    static RenderReport renderToBuffer (const juce::ValueTree& project,
                                        juce::AudioBuffer<float>& destination,
                                        const RenderOptions& options = {});

    /** Renders and writes a WAV. */
    static RenderReport renderToFile (const juce::ValueTree& project,
                                      const juce::File& destination,
                                      const RenderOptions& options = {});
};

} // namespace dew
