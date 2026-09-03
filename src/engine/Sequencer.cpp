#include "engine/Sequencer.h"

#include <cmath>

namespace dew
{

int Sequencer::materialLengthSteps (const EngineSnapshot& snapshot,
                                Transport::Mode mode,
                                int patternIndex)
{
    if (mode == Transport::Mode::pattern)
    {
        if (patternIndex < 0 || patternIndex >= (int) snapshot.patterns.size())
            return 0;

        return snapshot.patterns[(size_t) patternIndex].lengthSteps;
    }

    return snapshot.songLengthSteps();
}

void Sequencer::collect (const EngineSnapshot& snapshot,
                         Transport::Mode mode,
                         juce::int64 positionSamples,
                         int numSamples,
                         const TempoMap& tempoMap,
                         double sampleRate,
                         int patternIndexForPatternMode,
                         std::vector<NoteTrigger>& out)
{
    out.clear();

    if (numSamples <= 0 || sampleRate <= 0.0)
        return;

    const auto blockStart = (double) positionSamples;
    const auto blockEnd = blockStart + (double) numSamples;

    // The map, not a scalar rate.
    //
    // Multiplying by one samples-per-step is right only while the tempo is
    // constant, and the error against a ramp accumulates AGAINST the sample
    // counter - which is the one drift this codebase's timing design exists to
    // prevent. Constant, the map is the same multiply it always was.
    const auto samplesAtStep = [&tempoMap, sampleRate] (double step)
    {
        return tempoMap.secondsForSteps (step) * sampleRate;
    };

    const auto stepAtSample = [&tempoMap, sampleRate] (double samples)
    {
        return tempoMap.stepsForSeconds (samples / sampleRate);
    };

    // Step boundaries falling inside [blockStart, blockEnd). At a typical tempo
    // and block size this is zero or one step, so the loop below is short.
    auto firstStep = (juce::int64) std::ceil (stepAtSample (blockStart));
    const auto lastStep = (juce::int64) std::ceil (stepAtSample (blockEnd)) - 1;

    if (firstStep < 0)
        firstStep = 0;

    // Which step is being emitted, so a note's duration is measured from where
    // it actually starts rather than from the beginning of the song.
    juce::int64 currentStep = 0;

    const auto emit = [&] (const NoteSnapshot& note, int offset)
    {
        if (note.channelIndex < 0 || note.channelIndex >= (int) snapshot.channels.size())
            return;

        NoteTrigger trigger;
        trigger.sampleOffset = offset;
        trigger.channelIndex = note.channelIndex;
        trigger.pitch = note.pitch;
        trigger.velocity = note.velocity;
        // The span, not a rate times a length: a note that runs through a tempo
        // change lasts the musical length it was written with.
        trigger.durationSamples = (int) std::llround (samplesAtStep ((double) currentStep
                                                                        + (double) note.lengthSteps)
                                                      - samplesAtStep ((double) currentStep));
        out.push_back (trigger);
    };

    for (auto step = firstStep; step <= lastStep; ++step)
    {
        const auto stepStart = samplesAtStep ((double) step);
        currentStep = step;

        if (stepStart < blockStart || stepStart >= blockEnd)
            continue;

        const auto offset = juce::jlimit (0, numSamples - 1,
                                          (int) std::llround (stepStart - blockStart));

        if (mode == Transport::Mode::pattern)
        {
            if (patternIndexForPatternMode < 0
                || patternIndexForPatternMode >= (int) snapshot.patterns.size())
                continue;

            const auto& pattern = snapshot.patterns[(size_t) patternIndexForPatternMode];
            const auto local = (int) (step % (juce::int64) pattern.lengthSteps);

            for (const auto& note : pattern.notes)
                if (note.step == local)
                    emit (note, offset);
        }
        else
        {
            const auto stepsPerBar = snapshot.stepsPerBar();

            for (const auto& clip : snapshot.clips)
            {
                if (clip.patternIndex < 0 || clip.patternIndex >= (int) snapshot.patterns.size())
                    continue;

                // A muted, or un-soloed, playlist track schedules nothing.
                if (! clip.trackAudible)
                    continue;

                const auto clipStart = (juce::int64) clip.startBar * stepsPerBar;
                const auto clipEnd = clipStart + (juce::int64) clip.lengthBars * stepsPerBar;

                if (step < clipStart || step >= clipEnd)
                    continue;

                const auto& pattern = snapshot.patterns[(size_t) clip.patternIndex];

                // A clip longer than its pattern repeats it, as FL does.
                const auto local = (int) ((step - clipStart) % (juce::int64) pattern.lengthSteps);

                for (const auto& note : pattern.notes)
                    if (note.step == local)
                        emit (note, offset);
            }
        }
    }
}

} // namespace dew
