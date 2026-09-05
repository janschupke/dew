#include "io/OfflineRenderer.h"

#include "io/RenderFormats.h"

#include "io/SoundFontPool.h"

#include "io/SamplePool.h"

#include "io/MidiExporter.h"
#include "i18n/Strings.h"
#include "model/Ids.h"
#include "engine/RenderPost.h"

namespace dew
{

namespace
{

/** Which samples to render, and which of them to keep.

    `firstSample` is where the KEPT audio starts; everything before it is
    rendered and discarded so that the engine arrives at the in-point in the
    state it would have been in on the way past. See the note on OfflineRenderer
    for why that is not negotiable.
*/
struct RenderSpan
{
    juce::int64 firstSample = 0;  ///< first sample kept
    juce::int64 totalSamples = 0; ///< samples rendered, counted from zero
    juce::int64 stopAfterSamples = 0;
    juce::int64 numKept() const noexcept
    {
        return juce::jmax ((juce::int64) 0, totalSamples - firstSample);
    }
};

juce::Result planSpan (const EngineSnapshot& snapshot, const RenderOptions& options,
                       int patternIndex, RenderSpan& span)
{
    const auto materialSteps = Sequencer::materialLengthSteps (snapshot, options.mode,
                                                               patternIndex);

    if (materialSteps <= 0)
        return juce::Result::fail (options.mode == Transport::Mode::song
                                       ? "This project has nothing in its playlist to render."
                                       : "Pattern " + juce::String (options.patternId)
                                             + " is empty or does not exist.");

    // Through the snapshot's own map, which is why the map lives there: this
    // function builds no snapshot of its own and needs no RenderOptions field to
    // reach it. Without a tempo curve the map is the same multiply this was.
    const auto samplesAt = [&snapshot, &options] (double steps)
    { return snapshot.tempoMap->secondsForSteps (steps) * options.sampleRate; };

    const auto materialSamples = (juce::int64) std::llround (samplesAt ((double) materialSteps));
    const auto tailSamples = (juce::int64) std::llround (options.tailSeconds * options.sampleRate);

    if (! options.barRange.isEmpty())
    {
        // A range is expressed in bars, so it is the one scope whose length does
        // not depend on how long the material happens to be.
        const auto stepsPerBar = (double) snapshot.stepsPerBar();

        span.firstSample = (juce::int64) std::llround (
            samplesAt ((double) options.barRange.firstBar * stepsPerBar));
        const auto lastSample = (juce::int64) std::llround (
            samplesAt ((double) options.barRange.lastBar * stepsPerBar));

        span.totalSamples = lastSample + tailSamples;

        // Never let the transport wrap back to the top inside a range: past the
        // end of the material the range is silence, not the song again.
        span.stopAfterSamples = juce::jmin (lastSample, materialSamples);
    }
    else if (options.seconds > 0.0)
    {
        // An explicit length renders exactly that long and lets the material loop.
        span.firstSample = 0;
        span.totalSamples = (juce::int64) std::llround (options.seconds * options.sampleRate);
        span.stopAfterSamples = span.totalSamples;
    }
    else
    {
        // The material plays once and the rest is decay. The transport loops, so
        // the tail has to be produced by stopping it at the end - otherwise the
        // "tail" is the song starting over, which is both wrong and loud.
        span.firstSample = 0;
        span.totalSamples = materialSamples + tailSamples;
        span.stopAfterSamples = materialSamples;
    }

    span.totalSamples = juce::jmax ((juce::int64) 1, span.totalSamples);
    span.firstSample = juce::jlimit ((juce::int64) 0, span.totalSamples - 1, span.firstSample);

    return juce::Result::ok();
}

/** Renders one already-built snapshot through one already-prepared engine.

    Split out of renderToBuffer so that stems can drive it N times over the same
    engine: constructing an AudioEngine per stem churns thirty-two preallocated
    effect units for nothing.

    `progressFrom`/`progressTo` are this pass's slice of the whole job, so N
    stems report one continuous 0..1 rather than N sawtooths.
*/
RenderReport renderSnapshot (EngineSnapshot snapshot, AudioEngine& engine,
                             juce::AudioBuffer<float>& destination, const RenderOptions& options,
                             RenderProgress* progress, double progressFrom, double progressTo)
{
    RenderReport report;

    const auto patternIndex = snapshot.patternIndexForId (options.patternId);

    RenderSpan span;
    report.result = planSpan (snapshot, options, patternIndex, span);

    if (report.result.failed())
        return report;

    engine.prepare (options.sampleRate, options.blockSize);
    engine.publish (std::move (snapshot));
    engine.setMode (options.mode);
    engine.setCurrentPatternId (options.patternId);
    engine.rewind();
    engine.play();

    destination.setSize (2, (int) span.numKept());
    destination.clear();

    juce::AudioBuffer<float> block (2, options.blockSize);

    bool stopped = false;

    for (juce::int64 position = 0; position < span.totalSamples;)
    {
        if (progress != nullptr)
        {
            if (progress->cancelled.load (std::memory_order_relaxed))
            {
                report.cancelled = true;
                return report;
            }

            const auto fraction = (double) position / (double) span.totalSamples;
            progress->fraction.store (progressFrom + (progressTo - progressFrom) * fraction,
                                      std::memory_order_relaxed);
        }

        if (! stopped && position >= span.stopAfterSamples)
        {
            // Voices keep rendering while stopped, so releases ring out.
            engine.stop();
            stopped = true;
        }

        const auto thisBlock = (int) juce::jmin ((juce::int64) options.blockSize,
                                                 span.totalSamples - position);

        juce::AudioBuffer<float> view (block.getArrayOfWritePointers(), 2, 0, thisBlock);
        engine.processBlock (view);

        // Blocks before the in-point are rendered for their effect on the
        // engine's state and then thrown away.
        const auto keepFrom = juce::jmax (position, span.firstSample);
        const auto keepTo = juce::jmin (position + thisBlock, span.totalSamples);

        if (keepTo > keepFrom)
            for (int channel = 0; channel < 2; ++channel)
                destination.copyFrom (channel, (int) (keepFrom - span.firstSample), view, channel,
                                      (int) (keepFrom - position), (int) (keepTo - keepFrom));

        position += thisBlock;
    }

    if (progress != nullptr)
        progress->fraction.store (progressTo, std::memory_order_relaxed);

    const auto kept = (int) span.numKept();

    // Fades first, then normalize: what was asked for is that the FILE peaks at
    // the target, so the peak has to be measured on the buffer that will be
    // written. The other order leaves a file quieter than asked for whenever the
    // peak sits inside a fade.
    RenderPost::applyFades (destination, options.sampleRate, options.fadeInSeconds,
                            options.fadeOutSeconds);

    if (options.normalize)
        report.normalizationGainDb = RenderPost::normalize (
            destination, juce::Decibels::decibelsToGain (options.normalizePeakDb));

    // Measured after post-processing, so the numbers the UI and dew_render print
    // are the numbers in the file. Dither is not applied here - it belongs to the
    // destination's bit depth, which a float buffer does not have.
    report.numSamples = kept;
    report.seconds = (double) kept / options.sampleRate;
    report.peak = destination.getMagnitude (0, kept);
    report.rms = 0.5f
                 * (destination.getRMSLevel (0, 0, kept) + destination.getRMSLevel (1, 0, kept));

    return report;
}

/** Names, which the snapshot deliberately does not carry: a juce::String has no
    business in a struct the audio thread reads every block.
*/
juce::StringArray mixerTrackNames (const juce::ValueTree& project)
{
    juce::StringArray names;

    if (const auto mixer = project.getChildWithName (ids::MIXER); mixer.isValid())
        for (const auto& track : mixer)
            if (track.hasType (ids::MIXER_TRACK))
                names.add (track[ids::name].toString());

    return names;
}

} // namespace

RenderReport OfflineRenderer::renderToBuffer (const juce::ValueTree& project,
                                              juce::AudioBuffer<float>& destination,
                                              const RenderOptions& options,
                                              RenderProgress* progress)
{
    juce::StringArray warnings;
    auto snapshot = buildSnapshot (project, &warnings, options.samplePool, options.soundFontPool);

    AudioEngine engine;
    auto report = renderSnapshot (std::move (snapshot), engine, destination, options, progress, 0.0,
                                  1.0);

    report.warnings.addArray (warnings);
    return report;
}

RenderReport OfflineRenderer::renderToFile (const juce::ValueTree& project,
                                            const juce::File& destination,
                                            const RenderOptions& options, RenderProgress* progress)
{
    RenderReport report;

    // MIDI is not audio, so it does not go anywhere near the engine. It shares
    // this entry point because "what do you want out of this project" is one
    // question to the user.
    if (options.format == RenderFormat::midi)
    {
        MidiExportOptions midiOptions;
        midiOptions.mode = options.mode;
        midiOptions.patternId = options.patternId;
        midiOptions.barRange = options.barRange;

        auto midiReport = MidiExporter::writeToFile (project, destination, midiOptions);

        if (progress != nullptr)
            progress->fraction.store (1.0, std::memory_order_relaxed);

        return midiReport;
    }

    report.result = renderFormat::validateForFormat (options);

    if (report.result.failed())
        return report;

    juce::AudioBuffer<float> rendered;
    report = renderToBuffer (project, rendered, options, progress);

    if (! report.ok() || report.cancelled)
        return report;

    report.result = renderFormat::writeAudio (rendered, destination, options);

    if (report.ok())
        report.files.add (destination);

    return report;
}

RenderReport OfflineRenderer::renderStems (const juce::ValueTree& project, const juce::File& folder,
                                           const RenderOptions& options, RenderProgress* progress)
{
    RenderReport report;

    if (options.format == RenderFormat::midi)
    {
        report.result = juce::Result::fail ("Stems are audio; MIDI is one file.");
        return report;
    }

    report.result = renderFormat::validateForFormat (options);

    if (report.result.failed())
        return report;

    juce::StringArray warnings;
    const auto base = buildSnapshot (project, &warnings, options.samplePool, options.soundFontPool);
    report.warnings.addArray (warnings);

    const auto names = mixerTrackNames (project);
    const auto numTracks = (int) base.mixerTracks.size();

    if (numTracks <= 0)
    {
        report.result = juce::Result::fail ("This project has no mixer tracks to render.");
        return report;
    }

    if (! folder.createDirectory())
    {
        report.result = juce::Result::fail ("Could not create " + folder.getFullPathName());
        return report;
    }

    // A stem is a full render, so it goes through the master chain like anything
    // else. That is what makes each one sound the way it does in the mix - and it
    // is also why a non-linear master effect stops the stems summing back to it.
    if (base.masterEffects.anyEnabled())
        report.warnings.add (tr (StringId::warning_stemsDoNotSum));

    // One engine for every pass. A fresh AudioEngine preallocates thirty-two
    // effect units at their maximum size; prepare() resets voices, effect units
    // and the unit-type table, so it is a complete reset without the churn.
    AudioEngine engine;

    juce::StringArray silent;

    for (int track = 0; track < numTracks; ++track)
    {
        if (progress != nullptr)
        {
            if (progress->cancelled.load (std::memory_order_relaxed))
            {
                report.cancelled = true;
                return report;
            }

            progress->setStage ("Stem " + juce::String (track + 1) + " of "
                                + juce::String (numTracks));
        }

        auto stem = base;

        // Isolate by MUTING the others, which is the only way there is now:
        // mute is the one state a track has. It was already the right answer
        // while solo existed - a project with a track soloed still has to yield
        // a stem for every track - and it is why clearing the mixer-wide solo
        // flag had to be part of building a stem.
        for (int i = 0; i < numTracks; ++i)
            stem.mixerTracks[(size_t) i].mute = (i != track);

        juce::AudioBuffer<float> rendered;

        auto pass = renderSnapshot (std::move (stem), engine, rendered, options, progress,
                                    (double) track / (double) numTracks,
                                    (double) (track + 1) / (double) numTracks);

        if (pass.cancelled)
        {
            report.cancelled = true;
            return report;
        }

        if (! pass.ok())
        {
            report.result = pass.result;
            return report;
        }

        const auto name = juce::isPositiveAndBelow (track, names.size())
                                  && names[track].isNotEmpty()
                              ? names[track]
                              : "Track " + juce::String (track + 1);

        if (options.skipSilentStems && pass.peak <= 0.0f)
        {
            silent.add (name);
            continue;
        }

        // The index keeps the mixer's order, and disambiguates two inserts that
        // happen to have been given the same name.
        const auto fileName = juce::File::createLegalFileName (
                                  juce::String (track + 1).paddedLeft ('0', 2) + " " + name)
                              + extensionFor (options.format);

        const auto destination = folder.getChildFile (fileName);

        if (const auto result = renderFormat::writeAudio (rendered, destination, options);
            result.failed())
        {
            report.result = result;
            return report;
        }

        report.files.add (destination);

        // The loudest stem stands for the set. There is no single peak or rms
        // across N files, and reporting zero would read as "nothing came out".
        report.peak = juce::jmax (report.peak, pass.peak);
        report.rms = juce::jmax (report.rms, pass.rms);
        report.numSamples = juce::jmax (report.numSamples, pass.numSamples);
        report.seconds = juce::jmax (report.seconds, pass.seconds);
    }

    if (! silent.isEmpty())
        report.warnings.add (
            "Nothing was routed to " + silent.joinIntoString (", ")
            + ", so no file was written for "
            + (silent.size() == 1 ? juce::String ("it.") : juce::String ("them.")));

    if (report.files.isEmpty())
        report.result = juce::Result::fail ("Every stem was silent, so nothing was written.");

    return report;
}

} // namespace dew
