#include "engine/TempoMap.h"

#include <vector>

#include <algorithm>
#include <cmath>

#include "engine/EngineSnapshot.h"
#include "i18n/Strings.h"

namespace dew
{

namespace
{

/** Seconds one step lasts at a tempo. The ONE place the arithmetic is written;
    Transport::samplesPerStepFor multiplies this by a sample rate. */
double secondsPerStepAtTempo (double bpm, int stepsPerBeat) noexcept
{
    return 60.0 / juce::jmax (1.0, bpm) / (double) juce::jmax (1, stepsPerBeat);
}

} // namespace

TempoMap TempoMap::constant (double bpm, int stepsPerBeat) noexcept
{
    TempoMap map;
    map.constantTempo = true;
    map.constantSecondsPerStep = secondsPerStepAtTempo (bpm, stepsPerBeat);
    map.tempoSegments.push_back ({ 0.0, juce::jmax (1.0, bpm) });

    return map;
}

TempoMap TempoMap::build (const EngineSnapshot& snapshot, juce::StringArray* warnings)
{
    const auto fallback = [&snapshot]
    { return constant (snapshot.tempoBpm, snapshot.stepsPerBeat); };

    // Which clips drive the tempo. An audible automation clip whose automation
    // is scoped to the project and points at tempoBpm - nothing else can make
    // time itself move.
    //
    // COLLECTED, not counted. This pass already existed and threw its answer
    // away, and the per-step lookup below then re-asked the same question of
    // every clip in the arrangement - so building the map was O(steps x clips),
    // with steps bounded only by maxSteps. It runs on every coalesced document
    // change, which means once per message-loop turn while a knob is being
    // dragged: 400 clips over 400 bars took 2.4ms a turn, all of it spent
    // skipping the 399 clips that are not tempo curves.
    //
    // In clip order, so the FIRST match still wins where two overlap.
    std::vector<int> tempoClips;

    for (int i = 0; i < (int) snapshot.clips.size(); ++i)
    {
        const auto& clip = snapshot.clips[(size_t) i];

        if (clip.automationIndex < 0 || ! clip.trackAudible)
            continue;

        const auto& automation = snapshot.automations[(size_t) clip.automationIndex];

        if (automation.scope == AutomationScope::project
            && automation.param == AutomationParam::tempoBpm)
            tempoClips.push_back (i);
    }

    if (tempoClips.empty())
        return fallback();

    const auto tempoAt = [&snapshot, &tempoClips] (double step, double& bpm)
    {
        for (const auto index : tempoClips)
        {
            const auto& clip = snapshot.clips[(size_t) index];
            const auto& automation = snapshot.automations[(size_t) clip.automationIndex];

            const auto start = (double) clip.startStep;
            const auto end = start + (double) clip.lengthSteps;

            if (step < start || step >= end)
                continue;

            bpm = (double) automation.valueAt (step - start);
            return true;
        }

        return false;
    };

    const auto steps = snapshot.songLengthSteps();

    if (steps <= 0)
        return fallback();

    if (steps > maxSteps)
    {
        if (warnings != nullptr)
            warnings->add (tr (StringId::warning_tempoCurveTooLong,
                               Args {}.with ("tempo", juce::String (snapshot.tempoBpm, 1))));

        return fallback();
    }

    TempoMap map;
    map.constantTempo = false;
    map.constantSecondsPerStep = secondsPerStepAtTempo (snapshot.tempoBpm, snapshot.stepsPerBeat);

    map.stepStartSeconds.reserve ((size_t) steps + 1);
    map.stepStartSeconds.push_back (0.0);

    auto previousBpm = 0.0;

    for (int step = 0; step < steps; ++step)
    {
        // Sampled at the step's MIDPOINT, not its start. The map is a
        // piecewise-constant approximation of the integral of 1/bpm; midpoint
        // sampling is second-order accurate where left-edge sampling is
        // first-order, so a linear ramp comes out very nearly exact and a
        // stepped segment that jumps on a step boundary comes out exactly right.
        auto bpm = snapshot.tempoBpm;
        tempoAt ((double) step + 0.5, bpm);

        bpm = juce::jlimit (1.0, 999.0, bpm);

        if (! juce::approximatelyEqual (bpm, previousBpm))
        {
            map.tempoSegments.push_back ({ (double) step, bpm });
            previousBpm = bpm;
        }

        map.stepStartSeconds.push_back (map.stepStartSeconds.back()
                                        + secondsPerStepAtTempo (bpm, snapshot.stepsPerBeat));
    }

    return map;
}

double TempoMap::secondsForSteps (double steps) const noexcept
{
    if (constantTempo || stepStartSeconds.empty())
        return constantSecondsPerStep * steps;

    const auto last = (int) stepStartSeconds.size() - 1;

    // Before the table and past its end, the first and last step's duration
    // extrapolate. That keeps the map TOTAL and monotone, which matters because
    // a render's tail runs past the material and because wrappedIntoLoop
    // compares positions outside the window.
    if (steps <= 0.0)
        return steps * (stepStartSeconds[1] - stepStartSeconds[0]);

    if (steps >= (double) last)
    {
        const auto tail = stepStartSeconds[(size_t) last] - stepStartSeconds[(size_t) last - 1];

        return stepStartSeconds[(size_t) last] + (steps - (double) last) * tail;
    }

    const auto index = (size_t) std::floor (steps);
    const auto within = steps - (double) index;

    return stepStartSeconds[index]
           + within * (stepStartSeconds[index + 1] - stepStartSeconds[index]);
}

double TempoMap::stepsForSeconds (double seconds) const noexcept
{
    if (constantTempo || stepStartSeconds.empty())
        return constantSecondsPerStep > 0.0 ? seconds / constantSecondsPerStep : 0.0;

    const auto last = (int) stepStartSeconds.size() - 1;

    if (seconds <= stepStartSeconds.front())
    {
        const auto head = stepStartSeconds[1] - stepStartSeconds[0];

        return head > 0.0 ? seconds / head : 0.0;
    }

    if (seconds >= stepStartSeconds[(size_t) last])
    {
        const auto tail = stepStartSeconds[(size_t) last] - stepStartSeconds[(size_t) last - 1];

        return tail > 0.0 ? (double) last + (seconds - stepStartSeconds[(size_t) last]) / tail
                          : (double) last;
    }

    // The same entry the forward direction interpolates within, found by binary
    // search - which is what makes the two exact inverses of each other rather
    // than two approximations that agree most of the time.
    const auto it = std::upper_bound (stepStartSeconds.begin(), stepStartSeconds.end(), seconds);
    const auto index = (size_t) std::distance (stepStartSeconds.begin(), it) - 1;

    const auto span = stepStartSeconds[index + 1] - stepStartSeconds[index];

    return (double) index + (span > 0.0 ? (seconds - stepStartSeconds[index]) / span : 0.0);
}

double TempoMap::secondsPerStepAt (double steps) const noexcept
{
    if (constantTempo || stepStartSeconds.size() < 2)
        return constantSecondsPerStep;

    const auto last = (int) stepStartSeconds.size() - 1;
    const auto index = (size_t) juce::jlimit (0, last - 1,
                                              (int) std::floor (juce::jmax (0.0, steps)));

    return stepStartSeconds[index + 1] - stepStartSeconds[index];
}

} // namespace dew
