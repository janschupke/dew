#include "io/OfflineRenderer.h"

#include "io/SoundFontPool.h"

#include "io/SamplePool.h"

#include "io/MidiExporter.h"
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

/** The audio format to write with, or nullptr if this machine cannot.

    Held by unique_ptr because LAMEEncoderAudioFormat has to be constructed with
    the path to the binary, so these cannot all be static instances.
*/
std::unique_ptr<juce::AudioFormat> audioFormatFor (const RenderOptions& options)
{
    switch (options.format)
    {
        case RenderFormat::wav: return std::make_unique<juce::WavAudioFormat>();

        case RenderFormat::flac: return std::make_unique<juce::FlacAudioFormat>();

        case RenderFormat::mp3:
        {
#if JUCE_USE_LAME_AUDIO_FORMAT
            const auto lame = options.lameExecutable != juce::File() ? options.lameExecutable
                                                                     : OfflineRenderer::findLame();

            if (! lame.existsAsFile())
                return {};

            return std::make_unique<juce::LAMEEncoderAudioFormat> (lame);
#else
            return {};
#endif
        }

        case RenderFormat::midi: break;
    }

    return {};
}

/** Checks the things the format cares about but its writer does not.

    LAMEEncoderAudioFormat publishes getPossibleSampleRates() and
    getPossibleBitDepths() and then enforces neither: ask it for 96kHz and it
    builds a writer, hands lame a file it cannot use, and fails silently in a
    destructor. So the checking happens here, where it can say something.
*/
juce::Result validateForFormat (const RenderOptions& options)
{
    if (options.format == RenderFormat::mp3)
    {
        if (! OfflineRenderer::isAvailable (RenderFormat::mp3))
            return juce::Result::fail ("MP3 export needs the lame encoder, which was not found. "
                                       "Install it with `brew install lame`.");

        const auto rate = (int) std::llround (options.sampleRate);

        if (rate != 32000 && rate != 44100 && rate != 48000)
            return juce::Result::fail ("MP3 supports 32000, 44100 or 48000 Hz. This render is at "
                                       + juce::String (rate) + " Hz.");
    }

    if (options.format == RenderFormat::wav || options.format == RenderFormat::flac)
    {
        if (options.floatingPoint && options.bitDepth != 32)
            return juce::Result::fail ("A floating point file has to be 32-bit.");

        if (options.format == RenderFormat::flac && options.floatingPoint)
            return juce::Result::fail ("FLAC is an integer format; it cannot hold floats.");
    }

    return juce::Result::ok();
}

juce::AudioFormatWriterOptions writerOptionsFor (const RenderOptions& options)
{
    auto writerOptions = juce::AudioFormatWriterOptions()
                             .withSampleRate (options.sampleRate)
                             .withNumChannels (2);

    if (options.format == RenderFormat::mp3)
    {
        // 16 is the only depth lame's wrapper accepts, whatever was asked for.
        return writerOptions.withBitsPerSample (16).withQualityOptionIndex (
            juce::jlimit (0, juce::jmax (0, OfflineRenderer::mp3QualityOptions().size() - 1),
                          options.mp3QualityIndex));
    }

    writerOptions = writerOptions.withBitsPerSample (options.bitDepth);

    if (options.floatingPoint)
        writerOptions = writerOptions.withSampleFormat (
            juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);

    return writerOptions;
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

/** Dithers, then writes one buffer to one file, atomically.

    Shared by renderToFile and every stem, so the temporary-file swap, the
    format switch and the MP3 destructor rule exist in exactly one place.
*/
juce::Result writeAudio (juce::AudioBuffer<float>& buffer, const juce::File& destination,
                         const RenderOptions& options)
{
    // Dither belongs to the destination's LSB, so it happens here rather than in
    // renderToBuffer, which hands back a float buffer that has no bit depth. It
    // runs after the report was filled in, so peak and rms stay the numbers the
    // render produced rather than drifting by a fraction of an LSB.
    if (options.dither && ! options.floatingPoint)
        RenderPost::dither (buffer, options.bitDepth, options.ditherSeed);

    destination.getParentDirectory().createDirectory();

    // Write to a temporary and swap, as ProjectSerializer does. Writing in place
    // means a render that fails, or that the user cancels, destroys whatever
    // export was already sitting at that path.
    juce::TemporaryFile temp (destination);

    {
        auto fileStream = std::unique_ptr<juce::FileOutputStream> (
            temp.getFile().createOutputStream());

        if (fileStream == nullptr || ! fileStream->openedOk())
            return juce::Result::fail ("Could not create " + destination.getFullPathName());

        std::unique_ptr<juce::OutputStream> outputStream (std::move (fileStream));

        auto format = audioFormatFor (options);

        if (format == nullptr)
            return juce::Result::fail ("Cannot write " + OfflineRenderer::nameFor (options.format)
                                       + " on this machine.");

        auto writer = format->createWriterFor (outputStream, writerOptionsFor (options));

        if (writer == nullptr)
            return juce::Result::fail ("Could not create a "
                                       + OfflineRenderer::nameFor (options.format) + " writer at "
                                       + juce::String (options.bitDepth) + "-bit / "
                                       + juce::String (options.sampleRate, 0) + " Hz.");

        if (! writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()))
            return juce::Result::fail ("Could not write audio to " + destination.getFullPathName());
    }
    // The writer is destroyed HERE, and that brace is load-bearing. The MP3
    // writer streams to a temporary WAV and only runs lame when it is destroyed,
    // piping the result into the stream - so nothing exists until this point. It
    // also returns void and gives up silently after one retry, which is why the
    // size check below is required rather than defensive.

    if (temp.getFile().getSize() <= 0)
        return juce::Result::fail (OfflineRenderer::nameFor (options.format)
                                   + " encoding produced no output.");

    if (! temp.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("Could not replace " + destination.getFullPathName());

    return juce::Result::ok();
}

} // namespace

juce::String OfflineRenderer::extensionFor (RenderFormat format) noexcept
{
    switch (format)
    {
        case RenderFormat::wav: return ".wav";
        case RenderFormat::flac: return ".flac";
        case RenderFormat::mp3: return ".mp3";
        case RenderFormat::midi: return ".mid";
    }

    return ".wav";
}

juce::String OfflineRenderer::nameFor (RenderFormat format) noexcept
{
    switch (format)
    {
        case RenderFormat::wav: return "WAV";
        case RenderFormat::flac: return "FLAC";
        case RenderFormat::mp3: return "MP3";
        case RenderFormat::midi: return "MIDI";
    }

    return "WAV";
}

juce::StringArray OfflineRenderer::mp3QualityOptions()
{
#if JUCE_USE_LAME_AUDIO_FORMAT
    const auto lame = findLame();

    if (lame.existsAsFile())
        return juce::LAMEEncoderAudioFormat (lame).getQualityOptions();
#endif

    return {};
}

juce::File OfflineRenderer::findLame()
{
    // Cached: the answer cannot change while the app runs, and the UI asks
    // whenever it repaints a greyed-out menu entry.
    static const juce::File found = []
    {
        const auto path = juce::SystemStats::getEnvironmentVariable ("PATH", {});

        for (const auto& directory : juce::StringArray::fromTokens (path, ":", {}))
        {
            if (directory.isEmpty())
                continue;

            const auto candidate = juce::File (directory).getChildFile ("lame");

            if (candidate.existsAsFile())
                return candidate;
        }

        // A GUI app launched from Finder does not inherit a shell's PATH, so the
        // usual Homebrew locations have to be named.
        for (const auto* fallback :
             { "/opt/homebrew/bin/lame", "/usr/local/bin/lame", "/usr/bin/lame" })
            if (juce::File file { fallback }; file.existsAsFile())
                return file;

        return juce::File();
    }();

    return found;
}

bool OfflineRenderer::isAvailable (RenderFormat format)
{
    if (format != RenderFormat::mp3)
        return true;

#if JUCE_USE_LAME_AUDIO_FORMAT
    return findLame().existsAsFile();
#else
    return false;
#endif
}

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

    report.result = validateForFormat (options);

    if (report.result.failed())
        return report;

    juce::AudioBuffer<float> rendered;
    report = renderToBuffer (project, rendered, options, progress);

    if (! report.ok() || report.cancelled)
        return report;

    report.result = writeAudio (rendered, destination, options);

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

    report.result = validateForFormat (options);

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
        report.warnings.add ("The master chain processes each stem, so the stems will not sum "
                             "exactly back to the mix.");

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

        // Isolate by MUTING the others rather than soloing this one: isAudible
        // checks mute first, and clearing anySolo means a project that already
        // has a track soloed still yields a stem for every track, which is what
        // stems are for.
        for (int i = 0; i < numTracks; ++i)
            stem.mixerTracks[(size_t) i].mute = (i != track);

        stem.anySolo = false;

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

        if (const auto result = writeAudio (rendered, destination, options); result.failed())
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
