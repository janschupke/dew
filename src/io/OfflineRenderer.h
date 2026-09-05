#pragma once

#include <atomic>

#include <juce_audio_formats/juce_audio_formats.h>

#include "engine/AudioEngine.h"
#include "model/Constants.h"

namespace dew
{

/** What to write.

    `midi` is not audio at all - it is handled by MidiExporter and ignores every
    option about sample rate, depth and dynamics. It lives in the same enum
    anyway because "what do you want out of this project" is one question to the
    user, and one switch in renderToFile.
*/
enum class RenderFormat
{
    wav,
    flac,
    mp3,
    midi
};

/** A half-open range of bars, 0-based - the same convention ClipSnapshot::startBar
    uses, so a clip at startBar 4 is inside BarRange { 4, 5 }.

    An empty range means "the whole material", which is what every caller that
    predates ranges asks for by saying nothing.
*/
struct BarRange
{
    int firstBar = 0;
    int lastBar = 0;

    bool isEmpty() const noexcept
    {
        return lastBar <= firstBar;
    }
    int numBars() const noexcept
    {
        return juce::jmax (0, lastBar - firstBar);
    }
};

/** How to render. At namespace scope rather than nested in OfflineRenderer:
    a nested type's default member initializers are not usable in a default
    argument inside the enclosing class definition.

    Every field added after the first release defaults to a NEUTRAL value, so a
    caller written before it existed renders exactly what it always did. That is
    deliberate and load-bearing: the render tests are the only evidence that
    playback works, and a changed default would move them all at once.
*/
class SamplePool;
class SoundFontPool;

struct RenderOptions
{
    /** Where audio channels get their samples.

        Null renders them silent, which is right for a project that has none and
        wrong for one that does - so every caller that renders a real project
        passes one. Without it a song containing recordings would export as the
        synth parts alone, and say nothing about it.
    */
    SamplePool* samplePool = nullptr;

    /** Where soundfont channels get their fonts.

        The same rule, and the same failure if it is forgotten: a song using a
        soundfont would export as everything else, and say nothing about it.
        OfflineRenderer builds its OWN snapshot, so anything buildSnapshot needs
        has to arrive through here or be silently dropped from every render and
        every stem.
    */
    SoundFontPool* soundFontPool = nullptr;

    double sampleRate = kDefaultSampleRate;
    int blockSize = kDefaultBlockSize;
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

    /** Which bars to keep. Empty means all of them.

        The bars BEFORE the range are still rendered and then discarded - see
        the note on OfflineRenderer. Overrides `seconds` when set.
    */
    BarRange barRange;

    // --- post-processing -----------------------------------------------------
    /** Scale the result so it peaks at `normalizePeakDb`. Applied AFTER the
        fades, so the number is true of the file that gets written.
    */
    bool normalize = false;
    float normalizePeakDb = -1.0f;

    /** Ramps at the ends, so a range that starts mid-note does not click. */
    double fadeInSeconds = 0.0;
    double fadeOutSeconds = 0.0;

    /** Triangular dither when truncating to 16 bits or fewer. A no-op above
        that and for float destinations, so it is safe to leave on.
    */
    bool dither = true;
    juce::int64 ditherSeed = 0x5eed;

    // --- format --------------------------------------------------------------
    RenderFormat format = RenderFormat::wav;

    /** 32-bit IEEE float WAV. Requires bitDepth == 32; skips dither and the
        writer's clip at full scale, so a render that went over stays recoverable.
    */
    bool floatingPoint = false;

    /** Index into LAMEEncoderAudioFormat::getQualityOptions():
        0-9 are VBR best..smallest, 10-23 are CBR 32..320 kb/s.

        Clamped before use. Out of range is not a harmless mistake there - the
        lookup returns an empty string, which does not contain "VBR", so it falls
        through to the CBR branch and invokes lame with a bitrate of zero.
    */
    int mp3QualityIndex = 4;

    /** Where lame is. Empty means "go and find it". */
    juce::File lameExecutable;

    /** Skip a stem nothing is routed to. Without this a project with thirty-two
        inserts and four channels produces twenty-eight empty files.
    */
    bool skipSilentStems = true;
};

/** Progress out, cancellation in.

    Deliberately not a std::function inside RenderOptions: options are copied,
    compared and stored in settings, and a callback in there would make the
    struct none of those things. It would also raise a thread-affinity question
    this type does not have - the renderer only ever touches these atomics, and
    never calls back into its caller.

    The caller owns it and must outlive the render.
*/
struct RenderProgress
{
    // Spelled out because JUCE_DECLARE_NON_COPYABLE below declares a copy
    // constructor, and any user-declared constructor suppresses this one.
    RenderProgress() = default;

    /** 0..1 across the WHOLE job, including every stem. */
    std::atomic<double> fraction { 0.0 };

    /** Set from any thread. Honoured between blocks; see OfflineRenderer for
        what "between blocks" cannot cover.
    */
    std::atomic<bool> cancelled { false };

    /** What the job is doing, for a UI to show. Guarded rather than atomic
        because juce::String is not trivially copyable; contended at most at the
        rate a UI redraws.
    */
    juce::String getStage() const
    {
        const juce::ScopedLock l (lock);
        return stage;
    }
    void setStage (juce::String newStage)
    {
        const juce::ScopedLock l (lock);
        stage = std::move (newStage);
    }

private:
    juce::CriticalSection lock;
    juce::String stage;

    JUCE_DECLARE_NON_COPYABLE (RenderProgress)
};

struct RenderReport
{
    juce::Result result = juce::Result::ok();
    double seconds = 0.0;
    juce::int64 numSamples = 0;
    float peak = 0.0f;
    float rms = 0.0f;
    juce::StringArray warnings;

    /** True when the render stopped because it was asked to. NOT a failure:
        `result` stays ok, because nothing went wrong. A caller that treats a
        cancelled render as an error tells the user their own click was a bug.
    */
    bool cancelled = false;

    /** What normalize actually did, in dB. 0 when it was off or had nothing to
        scale - including when it hit its boost cap and gave up short.
    */
    float normalizationGainDb = 0.0f;

    /** What was actually written. One entry for renderToFile; one per stem
        otherwise, listing only the stems that made it.
    */
    juce::Array<juce::File> files;

    bool ok() const
    {
        return result.wasOk();
    }
};

/** Renders a project to audio without an audio device.

    This is how playback gets verified: in CI, in tests, and on any machine
    without working sound. It drives the same AudioEngine the live path does, so
    a passing render is evidence about the real engine and not about a
    simplified stand-in.

    Synchronous, and deliberately so. Being callable from any thread with no
    state of its own is what makes it testable; the background thread, its
    progress bar and its cancel button live in RenderJob, one layer up.

    A bar range renders from the START and throws away the bars before it,
    rather than seeking. This is not laziness and should not be "optimised":
    AudioEngine has a locate, and using it would be wrong. Sequencer only emits
    a trigger when a step boundary falls inside the block and the trigger
    carries the note's whole duration, so a note that began before the in-point
    would simply not exist - and a seek resets every voice on purpose, because
    live that is correct. Effect units are not reset by a seek either, so the
    reverb and delay would start empty and the range's first bar would sound
    audibly drier than the same bar inside a full render. There is no pre-roll
    length that fixes this in general: a 1000ms delay at 0.95 feedback rings for
    over two minutes, and a note's length has no upper bound.
*/
struct OfflineRenderer
{
    /** Renders into memory.

        Never dithers: the destination is float and has no bit depth, so there
        is no LSB to dither against.
    */
    static RenderReport renderToBuffer (const juce::ValueTree& project,
                                        juce::AudioBuffer<float>& destination,
                                        const RenderOptions& options = {},
                                        RenderProgress* progress = nullptr);

    /** Renders and writes one file, in whatever `options.format` asks for. */
    static RenderReport renderToFile (const juce::ValueTree& project, const juce::File& destination,
                                      const RenderOptions& options = {},
                                      RenderProgress* progress = nullptr);

    /** Renders one file per mixer track into `folder`.

        Each stem is a complete render with the other tracks muted, so it carries
        its own effect tails, its own automation and the master chain's response
        to it - which is what makes a stem sound the way that track sounds in the
        mix. The cost is one full render per track.
    */
    static RenderReport renderStems (const juce::ValueTree& project, const juce::File& folder,
                                     const RenderOptions& options = {},
                                     RenderProgress* progress = nullptr);

    /** The extension a format wants, including the dot. */
    static juce::String extensionFor (RenderFormat) noexcept;

    /** A human name for a format, for menus and messages. */
    static juce::String nameFor (RenderFormat) noexcept;

    /** False when a format needs something this machine does not have - which
        today means mp3 and a missing lame binary. The UI asks before offering it.
    */
    static bool isAvailable (RenderFormat);

    /** Where lame is, or a file that does not exist.

        Looks along PATH and then in the places Homebrew and a distribution put
        it. Cached: the answer cannot change without the app being restarted, and
        a UI that greys the MP3 option asks this every time it repaints.
    */
    static juce::File findLame();

    /** findLame's search, over a PATH handed in rather than the process's own.

        Split out to be testable: findLame caches in a static and reads the real
        environment, so nothing could reach the part that was wrong.
    */
    static juce::File findLameIn (const juce::String& pathVariable);

    /** The quality settings mp3 offers, in the order mp3QualityIndex means. */
    static juce::StringArray mp3QualityOptions();
};

} // namespace dew
