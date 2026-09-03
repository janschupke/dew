#include <catch2/catch_test_macros.hpp>

#include <type_traits>

#include "SourceScan.h"
#include "engine/AudioEngine.h"
#include "engine/EngineSnapshot.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"

using namespace dew;
using namespace dew::testing;

TEST_CASE ("what the render path copies owns nothing", "[realtime][snapshot]")
{
    // The render loop used to copy a ChannelSnapshot per channel per block, and
    // ChannelSnapshot carries a shared_ptr to the channel's audio -
    // EngineSnapshot.h spends fourteen lines saying that every copy and release
    // of that pointer belongs to the message thread, and calls the alternative
    // "a correctness bug, not a style question".
    //
    // Note what this test does NOT do. The obvious check - hold the shared_ptr,
    // read use_count(), render two hundred blocks, read it again - passes just
    // as happily with the bug as without it: the copy was made and destroyed
    // INSIDE processBlock, so the count between blocks never moved. A test that
    // cannot fail is worse than no test, so the guarantee is made structural
    // instead.
    // That the overrides own nothing is a static_assert inside AudioEngine,
    // where the types are - a compile error is a better home for that rule than
    // a runtime check, and it cannot be skipped.
    //
    // What is worth asserting here is the half that makes the rule necessary:
    // ChannelSnapshot really does own something, so copying one really does
    // touch a refcount. If this ever becomes trivially copyable the whole
    // concern has evaporated and this file should go.
    REQUIRE_FALSE (std::is_trivially_copyable_v<ChannelSnapshot>);
    REQUIRE_FALSE (std::is_trivially_copyable_v<EngineSnapshot>);
}

TEST_CASE ("the render path takes no snapshot entry by value", "[realtime][snapshot]")
{
    // The other half of the guarantee: the types are right, and nothing copies
    // the one that owns something. `auto x = snapshot.channels[i]` is a copy;
    // `const auto& x = ...` is not, and the difference is one character.
    const auto found = offenders (
        [] (const juce::String& line)
        {
            const auto trimmed = line.trim();

            if (! trimmed.startsWith ("auto "))
                return false;

            return trimmed.contains ("snapshot.channels[")
                   || trimmed.contains ("snapshot.mixerTracks[");
        },
        {});

    INFO ("snapshot entries copied rather than referenced:\n" << found.joinIntoString ("\n"));
    CHECK (found.isEmpty());
}

TEST_CASE ("instruments are built only for the channels that have them", "[realtime][instruments]")
{
    // Sixty-four SynthChannels - sixteen voices each, so 1,024 SynthVoice
    // objects - existed unconditionally, whether or not a project contained a
    // single synth. A project of audio channels paid for all of them.
    AudioEngine engine;
    engine.prepare (kDefaultSampleRate, 256);

    REQUIRE (engine.getMaterialisedInstrumentCount (InstrumentType::synth) == 0);
    REQUIRE (engine.getMaterialisedInstrumentCount (InstrumentType::audio) == 0);

    auto project = ProjectFactory::createDefault();

    juce::StringArray warnings;
    engine.setProject (project, &warnings);

    auto synths = 0;

    for (const auto& channel : project)
        if (channel.hasType (ids::CHANNEL))
            ++synths;

    REQUIRE (synths > 0);
    REQUIRE (engine.getMaterialisedInstrumentCount (InstrumentType::synth) == synths);
    REQUIRE (engine.getMaterialisedInstrumentCount (InstrumentType::audio) == 0);

    // Adding an audio channel makes one sampler, and leaves the synths alone -
    // a channel switched back to a synth should find its voices where it left
    // them, which is why nothing is ever destroyed.
    ProjectEdits::addAudioChannel (project, "Take", nullptr);
    engine.setProject (project, &warnings);

    REQUIRE (engine.getMaterialisedInstrumentCount (InstrumentType::synth) == synths);
    REQUIRE (engine.getMaterialisedInstrumentCount (InstrumentType::audio) == 1);
}
