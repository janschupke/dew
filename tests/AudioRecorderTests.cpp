#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/AudioEngine.h"
#include "io/AudioRecorder.h"
#include "io/LiveAudioHost.h"
#include "TestSupport.h"

using namespace dew::testing;

using namespace dew;

namespace
{

/** Feeds `numBlocks` blocks of a constant value straight to the audio-thread
    entry point. No device is opened anywhere in this file: the recorder's
    contract is a pointer and a count, which is exactly what makes it testable.
*/
void pushBlocks (AudioRecorder& recorder, float value, int numBlocks, int blockSize = 256)
{
    std::vector<float> left ((size_t) blockSize, value);
    const float* channels[] { left.data() };

    for (int i = 0; i < numBlocks; ++i)
        recorder.writeBlock (channels, 1, blockSize);
}

/** Reads a WAV back, as the render tests do. */
juce::AudioBuffer<float> readBack (const juce::File& file, double& sampleRate)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (file) };

    if (reader == nullptr)
        return {};

    sampleRate = reader->sampleRate;

    juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&buffer, 0, (int) reader->lengthInSamples, 0, true, true);
    return buffer;
}

} // namespace

TEST_CASE ("a recorder writes what the audio thread pushed into it", "[audio][record]")
{
    TempDir temp { "dew-record-" };
    const auto file = temp.dir.getChildFile ("take.wav");

    AudioRecorder recorder;
    REQUIRE (recorder.start (file, 44100.0, 1, 0).isEmpty());
    REQUIRE (recorder.isRecording());

    pushBlocks (recorder, 0.5f, 20);

    const auto written = recorder.stop();

    REQUIRE (! recorder.isRecording());
    REQUIRE (written == file);
    REQUIRE (file.existsAsFile());

    double rate = 0.0;
    const auto audio = readBack (file, rate);

    REQUIRE (rate == Catch::Approx (44100.0));
    REQUIRE (audio.getNumSamples() > 0);

    // 24-bit, so the round trip is close but not exact.
    REQUIRE (audio.getSample (0, 100) == Catch::Approx (0.5f).margin (0.001));
}

TEST_CASE ("the punch-in bar survives the take", "[audio][record]")
{
    // The transport has moved on by the time a take ends, so where the clip
    // goes has to be remembered when it starts.
    TempDir temp { "dew-record-" };

    AudioRecorder recorder;
    REQUIRE (recorder.start (temp.dir.getChildFile ("take.wav"), 44100.0, 1, 7).isEmpty());

    pushBlocks (recorder, 0.25f, 4);
    recorder.stop();

    REQUIRE (recorder.getPunchInBar() == 7);
}

TEST_CASE ("stopping without any input yields no file to use", "[audio][record]")
{
    // An armed channel with a dead input must not produce a silent clip that
    // looks like a successful take.
    TempDir temp { "dew-record-" };

    AudioRecorder recorder;
    REQUIRE (recorder.start (temp.dir.getChildFile ("take.wav"), 44100.0, 1, 0).isEmpty());

    REQUIRE (recorder.stop() == juce::File());
}

TEST_CASE ("writing when nothing is recording is harmless", "[audio][record]")
{
    // The callback pushes every block whether or not a take is running, because
    // the meter has to work before you commit to recording.
    AudioRecorder recorder;

    REQUIRE (! recorder.isRecording());
    pushBlocks (recorder, 0.9f, 4);

    REQUIRE (recorder.readAndClearInputPeak() == Catch::Approx (0.9f));
}

TEST_CASE ("the input meter reads and clears", "[audio][record]")
{
    // Read-and-clear, like the mixer's track meters: a meter that held its
    // highest value forever would never fall when the signal stopped.
    AudioRecorder recorder;

    pushBlocks (recorder, 0.4f, 1);
    REQUIRE (recorder.readAndClearInputPeak() == Catch::Approx (0.4f));
    REQUIRE (recorder.readAndClearInputPeak() == Catch::Approx (0.0f));
}

TEST_CASE ("the meter keeps the loudest peak between reads", "[audio][record]")
{
    AudioRecorder recorder;

    pushBlocks (recorder, 0.2f, 1);
    pushBlocks (recorder, 0.8f, 1);
    pushBlocks (recorder, 0.3f, 1);

    REQUIRE (recorder.readAndClearInputPeak() == Catch::Approx (0.8f));
}

TEST_CASE ("a second take does not append to the first", "[audio][record]")
{
    TempDir temp { "dew-record-" };
    const auto first = temp.dir.getChildFile ("one.wav");
    const auto second = temp.dir.getChildFile ("two.wav");

    AudioRecorder recorder;

    REQUIRE (recorder.start (first, 44100.0, 1, 0).isEmpty());
    pushBlocks (recorder, 0.5f, 40);
    recorder.stop();

    REQUIRE (recorder.start (second, 44100.0, 1, 0).isEmpty());
    pushBlocks (recorder, 0.5f, 4);
    recorder.stop();

    double rate = 0.0;
    REQUIRE (readBack (second, rate).getNumSamples() < readBack (first, rate).getNumSamples());
}

TEST_CASE ("a count-in is heard and not kept", "[audio][record][countin]")
{
    TempDir temp { "dew-record-" };
    const auto file = temp.dir.getChildFile ("take.wav");

    constexpr auto blockSize = 256;

    AudioRecorder recorder;
    REQUIRE (recorder.start (file, 44100.0, 1, 0, 3 * blockSize).isEmpty());
    REQUIRE (recorder.isPreRolling());

    pushBlocks (recorder, 0.5f, 3, blockSize);
    CHECK_FALSE (recorder.isPreRolling());

    pushBlocks (recorder, 0.5f, 10, blockSize);

    const auto written = recorder.stop();
    REQUIRE (written.existsAsFile());

    double rate = 0.0;
    const auto buffer = readBack (written, rate);

    // Ten blocks, not thirteen: the count-in is gone and nothing of the take
    // went with it.
    CHECK (buffer.getNumSamples() == 10 * blockSize);
}

TEST_CASE ("the input meter runs through a count-in", "[audio][record][countin]")
{
    TempDir temp { "dew-record-" };

    AudioRecorder recorder;
    REQUIRE (recorder.start (temp.dir.getChildFile ("take.wav"), 44100.0, 1, 0, 4096).isEmpty());

    pushBlocks (recorder, 0.5f, 1);

    // The half that the other implementation - skipping writeBlock entirely
    // while counting in - would have broken: the level display is exactly what
    // somebody is watching during a count-in.
    CHECK (recorder.readAndClearInputPeak() == Catch::Approx (0.5f));

    recorder.stop();
}

TEST_CASE ("a count-in shorter than a block still costs a whole one", "[audio][record][countin]")
{
    TempDir temp { "dew-record-" };
    const auto file = temp.dir.getChildFile ("take.wav");

    constexpr auto blockSize = 256;

    AudioRecorder recorder;
    REQUIRE (recorder.start (file, 44100.0, 1, 0, 1).isEmpty());

    pushBlocks (recorder, 0.5f, 2, blockSize);

    const auto written = recorder.stop();
    REQUIRE (written.existsAsFile());

    double rate = 0.0;

    // The block quantisation, stated rather than discovered. The engine's own
    // budget is spent the same way, which is what keeps the two in step - and
    // it errs towards the take starting AFTER the music, never before it.
    CHECK (readBack (written, rate).getNumSamples() == blockSize);
}

TEST_CASE ("a recorder refuses a file it cannot write", "[audio][record]")
{
    AudioRecorder recorder;

    const auto error = recorder.start (juce::File ("/this/path/does/not/exist/take.wav"), 44100.0,
                                       1, 0);

    REQUIRE (error.isNotEmpty());
    REQUIRE (! recorder.isRecording());
}

TEST_CASE ("the audio host reports honestly with no device open", "[audio][record][device]")
{
    // Deliberately not started, following AudioSettingsTests: asking for an
    // input with no device must produce a message rather than a silent no-op
    // that leaves the user watching a meter that can never move.
    AudioEngine engine;
    LiveAudioHost host { engine };

    REQUIRE (! host.isInputEnabled());
    REQUIRE (host.setInputEnabled (true).isNotEmpty());
    REQUIRE (! host.isInputEnabled());
}
