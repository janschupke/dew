#include "Sequencer.h"

#include <cmath>

namespace dew
{

int Sequencer::loopLengthSteps (const EngineSnapshot& snapshot,
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
                         double samplesPerStep,
                         int patternIndexForPatternMode,
                         std::vector<NoteTrigger>& out)
{
    out.clear();

    if (numSamples <= 0 || samplesPerStep <= 0.0)
        return;

    const auto blockStart = (double) positionSamples;
    const auto blockEnd = blockStart + (double) numSamples;

    // Step boundaries falling inside [blockStart, blockEnd). At a typical tempo
    // and block size this is zero or one step, so the loop below is short.
    auto firstStep = (juce::int64) std::ceil (blockStart / samplesPerStep);
    const auto lastStep = (juce::int64) std::ceil (blockEnd / samplesPerStep) - 1;

    if (firstStep < 0)
        firstStep = 0;

    const auto emit = [&] (const NoteSnapshot& note, int offset)
    {
        if (note.channelIndex < 0 || note.channelIndex >= (int) snapshot.channels.size())
            return;

        NoteTrigger trigger;
        trigger.sampleOffset = offset;
        trigger.channelIndex = note.channelIndex;
        trigger.pitch = note.pitch;
        trigger.velocity = note.velocity;
        trigger.durationSamples = (int) std::llround (samplesPerStep * (double) note.lengthSteps);
        out.push_back (trigger);
    };

    for (auto step = firstStep; step <= lastStep; ++step)
    {
        const auto stepStart = (double) step * samplesPerStep;

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
