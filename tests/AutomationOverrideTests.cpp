#include <cstring>
#include <set>

#include <catch2/catch_test_macros.hpp>

#include "engine/AutomationOverrides.h"
#include "engine/SnapshotReaders.h"
#include "model/ModuleCatalog.h"

using namespace dew;

namespace
{

/** An override with nothing set, and room for one oscillator slot and one
    effect slot to be addressed.

    Zeroed on purpose: every case below applies the value 1, so a parameter that
    reaches any field at all moves it - including the booleans, which is why
    starting from `false` matters. A fixture that had `enabled` already true
    would report a parameter as unapplied for writing the value it already had.
*/
ChannelOverrides emptyChannel()
{
    ChannelOverrides overrides;
    overrides.osc.numSlots = 1;
    overrides.effects.numSlots = 1;
    return overrides;
}

MixerTrackOverrides emptyTrack()
{
    MixerTrackOverrides overrides;
    overrides.effects.numSlots = 1;
    return overrides;
}

/** Whether applying this changed anything at all.

    A byte comparison rather than a field-by-field one, which is the whole point:
    a field-by-field check would have to name the fields, and naming them is the
    thing that goes out of date. Both objects are copies of one another, so
    padding agrees; the static_asserts on the engine's own members are what keep
    these trivially copyable.

    Two values, and that is not belt and braces. The fixture starts from the
    snapshot structs' own defaults, and several of those are 1 or true - a
    soundfont's three scaling offsets, an effect slot's bypass, an oscillator's
    on. Probing with one value would report every one of those as unapplied for
    writing what was already there, which is a gate that fails on the parameters
    that work.
*/
template <typename Overrides> bool moves (const Overrides& before, ActiveAutomation active)
{
    for (const auto value : { 0.0f, 1.0f })
    {
        active.value = value;

        auto after = before;
        applyAutomation (after, active);

        if (std::memcmp (&before, &after, sizeof (Overrides)) != 0)
            return true;
    }

    return false;
}

/** Every parameter that reaches a slot through `paramIndex` rather than by
    name, derived from the effect catalog rather than listed.

    These are the ones applyAutomation deliberately does not name: the index is
    resolved on the message thread, where the slot's type is known, which is the
    fix that replaced a sixteen-case switch. Deriving the set means a new effect
    parameter joins it by being declared, not by being remembered here.
*/
std::set<AutomationParam> paramsAppliedByIndex()
{
    std::set<AutomationParam> byIndex;

    for (int type = 0; type < kNumEffectTypes; ++type)
        for (const auto& spec : effectParamsFor ((EffectType) type))
        {
            const auto param = snapshotRead::automationParamFromIdentifier (
                AutomationScope::channelEffect, *spec.property);

            if (param != AutomationParam::none && param != AutomationParam::enabled)
                byIndex.insert (param);
        }

    return byIndex;
}

} // namespace

TEST_CASE ("every automation parameter reaches something", "[automation][engine][gate]")
{
    // The failure this gate exists for is silent in the worst way. A parameter
    // is declared automatable in the catalog, offered by the picker, drawn as a
    // curve, resolved by the reader - and if the override ladder has no line
    // for it, it moves a line on screen and nothing in the sound. Four
    // oscillator parameters were in exactly that state, for as long as the
    // ladder was the only thing that knew.
    //
    // So every enumerator is driven, under every scope that could apply it, and
    // one of them has to move a byte.
    const auto byIndex = paramsAppliedByIndex();

    REQUIRE (byIndex.size() > 20); // the control case: a derived set that came back empty

    const auto channel = emptyChannel();
    const auto track = emptyTrack();

    for (auto i = (int) AutomationParam::none + 1; i < (int) AutomationParam::count; ++i)
    {
        const auto param = (AutomationParam) i;

        // The one enumerator no override applies, and it says so where it is
        // declared: the tempo changes how steps become time, which is the
        // TempoMap's work and not a channel's.
        if (param == AutomationParam::tempoBpm)
            continue;

        // The index, because there is no name for an AutomationParam and
        // inventing one for a failure message would be a second list to keep
        // in step. Count from `none` in EngineSnapshot.h.
        INFO ("AutomationParam #" << i << ", counting from none in EngineSnapshot.h");

        if (byIndex.count (param) != 0)
        {
            // Through the index, at every position a block holds - so this also
            // says the block is wide enough for what the catalog declares.
            auto reached = false;

            for (auto index = 0; index < kMaxEffectParams; ++index)
                reached = reached
                          || moves (channel,
                                    { AutomationScope::channelEffect, 0, 0, param, index, 1.0f });

            CHECK (reached);
            continue;
        }

        // Named scopes. A parameter belongs to exactly one of these, but which
        // one is what the ladder decides, so the gate asks all of them rather
        // than repeating the mapping and going out of date with it.
        const auto
            reached = moves (channel, { AutomationScope::channel, 0, -1, param, -1, 1.0f })
                      || moves (channel, { AutomationScope::channelOsc, 0, 0, param, -1, 1.0f })
                      || moves (channel, { AutomationScope::channelAmp, 0, -1, param, -1, 1.0f })
                      || moves (channel,
                                { AutomationScope::channelSoundFont, 0, -1, param, -1, 1.0f })
                      || moves (channel, { AutomationScope::channelEffect, 0, 0, param, -1, 1.0f })
                      || moves (track, { AutomationScope::mixerTrack, 0, -1, param, -1, 1.0f })
                      || moves (track, { AutomationScope::mixerEffect, 0, 0, param, -1, 1.0f });

        CHECK (reached);
    }
}

TEST_CASE ("an automation writes only the slot it addresses", "[automation][engine]")
{
    // The bounds the ladder checks, asked the way a corrupt or stale snapshot
    // would ask them: a slot index past the end writes nothing rather than past
    // the array. -1 is the value a reader leaves when nothing resolved.
    const auto channel = emptyChannel();

    CHECK_FALSE (
        moves (channel, { AutomationScope::channelOsc, 0, -1, AutomationParam::gain, -1, 1.0f }));
    CHECK_FALSE (
        moves (channel, { AutomationScope::channelOsc, 0, 9, AutomationParam::gain, -1, 1.0f }));
    CHECK_FALSE (moves (
        channel, { AutomationScope::channelEffect, 0, 9, AutomationParam::enabled, -1, 1.0f }));

    // And an effect parameter index past the block, which is the one bound
    // writeEffectParam owns rather than the caller.
    CHECK_FALSE (moves (channel, { AutomationScope::channelEffect, 0, 0, AutomationParam::cutoff,
                                   kMaxEffectParams, 1.0f }));
}
