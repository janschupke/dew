#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include "io/OfflineRenderer.h"

namespace dew::renderFormat
{

/** Everything about the FILE a render is written into.

    INTERNAL. io/OfflineRenderer.h is the public surface; only OfflineRenderer's
    two translation units include this.

    Split from the render itself because the two are different subjects that
    happened to share a file. Nothing here touches an AudioEngine or an
    EngineSnapshot, and nothing in OfflineRenderer.cpp touches a
    juce::AudioFormat - so a change to how MP3 is encoded and a change to how a
    span is rendered can no longer be in the same diff by accident.

    MP3 is the reason this surface exists at all: JUCE can only decode it, so
    encoding drives an installed `lame` as a child process. Without it the
    format reports itself unavailable and everything else still works.
*/

/** Refuses a request the chosen format cannot honour, naming what is wrong. */
juce::Result validateForFormat (const RenderOptions& options);

/** Writes a rendered buffer, having validated it. */
juce::Result writeAudio (juce::AudioBuffer<float>& buffer, const juce::File& destination,
                         const RenderOptions& options);

} // namespace dew::renderFormat
