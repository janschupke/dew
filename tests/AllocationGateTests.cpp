#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdlib>
#include <new>
#include <vector>

#include "engine/AudioEngine.h"
#include "engine/EngineSnapshot.h"
#include "engine/Sequencer.h"
#include "engine/TempoMap.h"
#include "engine/SampleProvider.h"
#include "engine/SoundFontProvider.h"
#include "io/SoundFontFile.h"
#include "model/AutomationTargets.h"
#include "model/EffectType.h"
#include "model/GeneratorCatalog.h"
#include "model/Ids.h"
#include "model/ModuleCatalog.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "model/ProjectSchema.h"

#include "EffectDspHarness.h"
#include "FixtureSoundFont.h"

using namespace dew;

// =============================================================================
// The realtime invariant, made structural.
//
// EngineSnapshot.h and AudioEngine.h both state the rule - "a bounded queue that
// drops is honest; one that allocates to avoid dropping is not realtime" - and
// until this file the only thing enforcing it was a regex in RealtimeTests.cpp
// looking for ONE spelling of ONE mistake. Two vectors had drifted past it:
// Sequencer::collect appended a trigger per note with no bound, and
// collectAutomation appended per CLIP into a vector reserved per AUTOMATION.
//
// Counting the allocations instead catches every spelling, including the ones
// nobody has thought of. The replacement below is program-wide - operator new is
// a replaceable function - so it is deliberately almost free when disarmed: one
// relaxed load per allocation for the rest of the suite.
// =============================================================================

// A sanitizer that owns the allocator must be left to own it: ASan replaces
// operator new itself and intercepts the malloc underneath, and two
// replacements of the same function is not a thing to reason about.
//
// Not a measured claim about this machine. ASan and TSan cannot run here at
// all - on macOS 26.4 with Xcode 26.4, `int main(){}` built with
// -fsanitize=address spins forever inside __asan::InitializeShadowMemory, and
// the same program under -fsanitize=thread segfaults. UBSan is fine. So the
// asan and tsan presets are CI-only for now, and this guard is written from
// the contract rather than from an experiment.
//
// No loss of cover either way. This gate's job is the `ci` preset, which is
// what a change has to pass; the sanitizer builds exist to find other bugs.
// The tests skip rather than silently passing, so a build that is not checking
// this says so.
#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define DEW_SANITIZER_OWNS_THE_ALLOCATOR 1
#endif
#endif

namespace
{

// THREAD-LOCAL, and that is the whole correctness of this file rather than a
// detail of it.
//
// operator new is replaced program-wide, so a process-wide flag counts every
// thread in the process - and on macOS this process has threads dew did not
// start. LaunchServices keeps an XPC connection whose event handler runs on a
// libdispatch queue and allocates a std::vector when a notification arrives,
// at a moment nothing here controls. Armed process-wide, that lands inside the
// window every few runs and the gate fails naming dew's render path for an
// allocation in _MyCFXPCCreateCFObjectFromXPCObject.
//
// It was invisible while the window was short. Widening the fixture to cover
// the effect chains, the FM loop and the two sampled sources made the window
// long enough to hit it about three runs in ten - a flaky gate, which is worse
// than a narrow one, because it is the kind people learn to re-run.
//
// thread_local answers the question the gate means to ask: did the thread that
// is rendering allocate. A stray allocation on a system thread is not a defect
// in the render path and must not be reported as one.
thread_local bool counting = false;
thread_local int allocations = 0;

// Only the replaced operator new calls this, and that is only replaced when a
// sanitizer has not already taken the allocator - so under asan and tsan it is
// a function nobody calls, which -Wunused-function makes an error.
#if ! defined(DEW_SANITIZER_OWNS_THE_ALLOCATOR)
inline void noteAllocation() noexcept
{
    if (counting)
        ++allocations;
}
#endif

/** Arms the counter for a scope, and always disarms - a REQUIRE that throws
    inside the window must not leave every later test counting.

    Arms the CALLING thread only. Every test here renders on its own thread, so
    that is the thread whose allocations are the finding.
*/
struct ScopedAllocationCount
{
    ScopedAllocationCount()
    {
        allocations = 0;
        counting = true;
    }

    ~ScopedAllocationCount()
    {
        counting = false;
    }

    int count() const noexcept
    {
        return allocations;
    }
};

} // namespace

#if ! defined(DEW_SANITIZER_OWNS_THE_ALLOCATOR)

void* operator new (std::size_t size)
{
    noteAllocation();

    if (size == 0)
        size = 1;

    if (auto* p = std::malloc (size))
        return p;

    throw std::bad_alloc();
}

void* operator new[] (std::size_t size)
{
    return ::operator new (size);
}

void* operator new (std::size_t size, const std::nothrow_t&) noexcept
{
    noteAllocation();
    return std::malloc (size == 0 ? 1 : size);
}

void* operator new[] (std::size_t size, const std::nothrow_t& tag) noexcept
{
    return ::operator new (size, tag);
}

void* operator new (std::size_t size, std::align_val_t alignment)
{
    noteAllocation();

    const auto align = (std::size_t) alignment;

    // aligned_alloc requires a size that is a multiple of the alignment.
    if (size == 0)
        size = align;

    size = ((size + align - 1) / align) * align;

    if (auto* p = std::aligned_alloc (align, size))
        return p;

    throw std::bad_alloc();
}

void* operator new[] (std::size_t size, std::align_val_t alignment)
{
    return ::operator new (size, alignment);
}

void operator delete (void* p) noexcept
{
    std::free (p);
}
void operator delete[] (void* p) noexcept
{
    std::free (p);
}
void operator delete (void* p, std::size_t) noexcept
{
    std::free (p);
}
void operator delete[] (void* p, std::size_t) noexcept
{
    std::free (p);
}
void operator delete (void* p, const std::nothrow_t&) noexcept
{
    std::free (p);
}
void operator delete[] (void* p, const std::nothrow_t&) noexcept
{
    std::free (p);
}
void operator delete (void* p, std::align_val_t) noexcept
{
    std::free (p);
}
void operator delete[] (void* p, std::align_val_t) noexcept
{
    std::free (p);
}
void operator delete (void* p, std::size_t, std::align_val_t) noexcept
{
    std::free (p);
}
void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept
{
    std::free (p);
}

#endif // ! DEW_SANITIZER_OWNS_THE_ALLOCATOR

// Built only for the bodies that count allocations, which is the same condition
// again: every test below that uses this fixture SKIPs when a sanitizer owns the
// allocator, so building it there is a function nobody calls.
#if ! defined(DEW_SANITIZER_OWNS_THE_ALLOCATOR)

namespace
{

/** A provider that answers with one font, whatever it is asked for.

    The engine opens no files, so the only way a soundfont reaches the render
    path in a test is through this interface. Built in memory by
    SoundFontBuilder rather than read from a committed .sf2, which is what lets
    the whole suite run with no asset beside it.
*/
struct OneFontProvider : SoundFontProvider
{
    std::shared_ptr<const SoundFontData> font;

    std::shared_ptr<const SoundFontData> soundFontFor (const juce::String&) override
    {
        return font;
    }
};

/** The same, for an audio channel's take. */
struct OneSampleProvider : SampleProvider
{
    std::shared_ptr<const juce::AudioBuffer<float>> audio;

    std::shared_ptr<const juce::AudioBuffer<float>> audioFor (const juce::String&,
                                                              double& sourceSampleRate) override
    {
        sourceSampleRate = 48000.0;
        return audio;
    }
};

/** A project built to be as hard on the render path as the schema permits.

    Deliberately past every bound rather than near it: enough channels to fill
    the rack, a note on the SAME step of every one of them so one block starts
    them all at once, and more overlapping automation clips than
    kMaxAutomations, which is the case that used to reallocate.

    It also has to be past every BRANCH, which for a long time it was not. The
    fixture filled the rack with synth channels and nothing else, so
    `chain.anyEnabled()` was false everywhere, `bank.anyFm` was false, and no
    channel carried a take or a font. Four of the five things processBlock can
    render - every effect body, the FM voice loop, SamplePlayer and
    SoundFontChannel - were outside the armed window, and the gate reported a
    clean count for paths it had never entered. That is the failure this file's
    own comments name; it was true of this file.

    The audio and soundfont channels are added FIRST, before the rack is
    filled. buildSnapshot drops channels past kMaxChannels in document order,
    so a channel appended after the loop would be dropped and the path it
    exists to reach would go unmeasured - silently, which is the whole problem
    again.
*/
juce::ValueTree maximalProject()
{
    auto project = ProjectFactory::createDefault();

    // First, so the kMaxChannels cap below cannot drop them.
    auto audioChannel = ProjectEdits::addAudioChannel (project, "Take", nullptr);
    ProjectEdits::setSampleSource (audioChannel, "take.wav", 48000, 48000, nullptr);

    auto fontChannel = ProjectEdits::addSoundFontChannel (project, "Font", nullptr);
    ProjectEdits::setSoundFontSource (fontChannel, "font.sf2", 0, 0, "Ramp", nullptr);

    for (int i = 0; i < kMaxChannels; ++i)
        ProjectEdits::addChannel (project, "Ch" + juce::String (i), nullptr);

    auto pattern = project.getChildWithName (ids::PATTERN);

    // Every channel firing on the same step is what makes one block collect the
    // most triggers it ever can.
    for (const auto& channel : project)
    {
        if (! channel.hasType (ids::CHANNEL))
            continue;

        const int id = channel.getProperty (ids::id);

        for (int step = 0; step < 16; ++step)
            ProjectEdits::addNote (pattern, id, step, 4, 60, 1.0f, nullptr);

        // Every slot's LFO on, and asking for all three destinations, so the
        // voice's LFO pass and the instrument's panned stage are both INSIDE
        // the armed window. A fixture that left them off would report a clean
        // count for a path it had not measured - which is the failure mode
        // every gate in this file exists to avoid.
        for (int slotIndex = 0; slotIndex < kMaxOscillators; ++slotIndex)
        {
            auto slot = ProjectEdits::oscillatorAt (channel, slotIndex);
            auto lfo = generatorNodeFor (slot, ids::lfoOn);

            slot.setProperty (ids::enabled, true, nullptr);
            lfo.setProperty (ids::lfoOn, true, nullptr);
            lfo.setProperty (ids::lfoToPitch, 3.0, nullptr);
            lfo.setProperty (ids::lfoToVolume, 0.5, nullptr);
            lfo.setProperty (ids::lfoToPan, 0.8, nullptr);

            // A bent matrix, so the voice latches fmActive and runs the SECOND
            // render loop. The defaults are the identity - every amount zero,
            // every output one - and a voice that latched a false anyFm runs
            // the loop that predates the matrix, expression for expression. So
            // an untouched fixture measures SynthVoice::renderAdd twice and
            // SynthVoice::renderAddFm never.
            slot.setProperty (ids::fmTo1, 0.4, nullptr);
            slot.setProperty (ids::fmTo2, 0.3, nullptr);
            slot.setProperty (ids::fmTo3, 0.2, nullptr);
            slot.setProperty (ids::fmOut, 0.9, nullptr);
        }
    }

    // A full chain on every owner the schema offers one to, cycling through the
    // types so all ten process() bodies are inside the window. Nine per chain
    // is kMaxEffectsPerChain, and addEffect refuses past it rather than
    // growing, so this saturates rather than overflowing.
    //
    // Cycling rather than "one of each on one channel": a chain holds nine and
    // there are ten types, so no single chain can cover them.
    auto effectType = 0;

    for (auto owner : ProjectEdits::effectChainOwners (project))
    {
        for (int slot = 0; slot < kMaxEffectsPerChain; ++slot)
        {
            const auto type = effectTypeToString ((EffectType) (effectType % kNumEffectTypes));
            ++effectType;

            ProjectEdits::addEffect (project, owner, type, nullptr);
        }
    }

    // A PATTERN clip, and an AUDIO clip, both over bar 0 where the playhead
    // starts.
    //
    // Without the first of these the fixture played nothing at all. The test
    // runs in song mode - it has to, because collectAutomation only runs there
    // - and in song mode the playlist decides what sounds, not the pattern.
    // The rack full of channels, the note on every one of them and every slot's
    // LFO were all real in the document and none of them ever reached the audio
    // thread: SynthVoice::start was not called once inside the armed window, so
    // the gate was measuring the metronome, the automation walk and the mixer,
    // and no voice. Verified by making start() allocate and watching the gate
    // stay green.
    {
        auto notes = ProjectEdits::addPlaylistTrack (project, "Notes", nullptr);
        ProjectEdits::addClip (notes, (int) pattern[ids::id], 0 * 16, 4 * 16, nullptr);

        auto take = ProjectEdits::addPlaylistTrack (project, "Take", nullptr);
        ProjectEdits::addAudioClip (take, (int) audioChannel[ids::id], 0 * 16, 4 * 16, nullptr);
    }

    // More automation CLIPS than the active-automation vector is reserved for,
    // all covering bar 0, where the playhead starts - but only a handful of
    // automation DEFINITIONS between them.
    //
    // That asymmetry is the bug this guards. buildSnapshot caps automations at
    // kMaxAutomations, so a fixture of one clip per curve can never exceed the
    // reserve; clips are not capped at all, and several may carry the same
    // curve. collectAutomation walks clips, so that is what has to be piled up.
    const auto targets = availableAutomationTargets (project);

    std::vector<int> automationIds;

    for (int i = 0; i < 4 && i < (int) targets.size(); ++i)
    {
        auto automation = ProjectEdits::addAutomation (project, targets[(size_t) i], nullptr);
        automationIds.push_back ((int) automation.getProperty (ids::id));
    }

    REQUIRE_FALSE (automationIds.empty());

    for (int i = 0; i < kMaxAutomations * 2; ++i)
    {
        auto track = ProjectEdits::addPlaylistTrack (project, "A" + juce::String (i), nullptr);

        ProjectEdits::addAutomationClip (track, automationIds[(size_t) i % automationIds.size()],
                                         0 * 16, 4 * 16, nullptr);
    }

    return project;
}

} // namespace

#endif // ! DEW_SANITIZER_OWNS_THE_ALLOCATOR

TEST_CASE ("the allocation counter actually counts", "[realtime][gate][allocation]")
{
#if defined(DEW_SANITIZER_OWNS_THE_ALLOCATOR)
    SKIP ("the sanitizer owns operator new; this gate runs in the ci preset");
#else
    // The control case. Every gate in this repo that scans for something needs
    // one, because "found nothing" and "looked nowhere" are the same result.
    auto allocated = 0;

    {
        ScopedAllocationCount counter;

        std::vector<int> forcedToAllocate;
        forcedToAllocate.reserve (1024);

        allocated = counter.count();
    }

    CHECK (allocated > 0);
#endif
}

TEST_CASE ("the render path allocates nothing", "[realtime][gate][allocation]")
{
#if defined(DEW_SANITIZER_OWNS_THE_ALLOCATOR)
    SKIP ("the sanitizer owns operator new; this gate runs in the ci preset");
#else
    constexpr auto sampleRate = 48000.0;
    constexpr auto blockSize = 256;

    AudioEngine engine;
    engine.prepare (sampleRate, blockSize);

    // The providers have to outlive setProject, which is where the snapshot
    // fetches through them. dew_engine opens no files, so without these the
    // audio and soundfont channels resolve to nothing and render nothing -
    // which is how they stayed outside this window for so long.
    OneFontProvider fonts;
    {
        const auto bytes = testing::SoundFontBuilder::minimal().build();
        auto parsed = SoundFontFile::parse (bytes.getData(), bytes.getSize(), "Fixture");
        REQUIRE (parsed.isValid());
        fonts.font = std::make_shared<const SoundFontData> (std::move (parsed.font));
    }

    OneSampleProvider samples;
    {
        auto take = std::make_shared<juce::AudioBuffer<float>> (2, 48000);

        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < take->getNumSamples(); ++i)
                take->setSample (channel, i, (float) std::sin (i * 0.01) * 0.5f);

        samples.audio = take;
    }

    engine.setSoundFontPool (&fonts);
    engine.setSamplePool (&samples);

    juce::StringArray warnings;
    auto project = maximalProject();

    auto automationClips = 0;

    for (const auto& track : project.getChildWithName (ids::PLAYLIST))
        for (const auto& clip : track)
            if (ProjectEdits::isAutomationClip (clip))
                ++automationClips;

    // The fixture has to be past the bound, or this test passes for the wrong
    // reason - which is exactly how the bug it guards got in.
    INFO ("automation clips covering bar 0: " << automationClips);
    REQUIRE (automationClips > kMaxAutomations);

    engine.setProject (project, &warnings);

    // The fixture must PROVE it reached each branch, not merely intend to. Every
    // check below corresponds to a render path that was outside this window
    // until the fixture grew: a snapshot that quietly resolved none of them
    // would leave the allocation count green and meaningless.
    {
        // The same call setProject makes, with the same providers, so what is
        // asserted here is what the engine is about to render.
        juce::StringArray snapshotWarnings;
        const auto snapshot = buildSnapshot (project, &snapshotWarnings, &samples, &fonts);

        auto withFm = 0, withEffects = 0, withAudio = 0, withFont = 0;

        for (const auto& channel : snapshot.channels)
        {
            if (channel.osc.anyFm)
                ++withFm;
            if (channel.effects.anyEnabled())
                ++withEffects;
            if (channel.audio != nullptr)
                ++withAudio;
            if (channel.soundFont != nullptr)
                ++withFont;
        }

        INFO (snapshotWarnings.joinIntoString ("\n"));
        CHECK (withFm > 0);      // SynthVoice::renderAddFm
        CHECK (withEffects > 0); // every EffectModule::process
        CHECK (withAudio == 1);  // SamplePlayer::renderAdd
        CHECK (withFont == 1);   // SoundFontChannel / SoundFontVoice
    }

    // Song mode, not pattern: collectAutomation only runs in song mode, and the
    // automation clips below are the whole point of the fixture.
    engine.setMode (Transport::Mode::song);

    // The click ON, and a count-in armed for the first few blocks, so BOTH of
    // renderMetronome's branches are inside the armed window below. A fixture
    // that left the metronome off would report a clean count for a path it had
    // not measured, which is the failure mode every gate in this file exists to
    // avoid. Eight blocks of it, so the remaining 392 still exercise playing.
    engine.setMetronomeEnabled (true);
    engine.playWithCountIn (8 * blockSize);

    juce::AudioBuffer<float> block (2, blockSize);

    // Captured BEFORE any rendering, so they are the sizes prepare() reserved.
    //
    // Taking them after a warm-up hides the failure this exists to catch: a
    // vector that grows past its reserve does so ONCE, doubling, and every
    // later block then fits. Warming up first and comparing afterwards measures
    // the steady state of an already-broken engine.
    const auto triggerCapacity = engine.getTriggerCapacity();
    const auto automationCapacity = engine.getActiveAutomationCapacity();

    // No warm-up, deliberately. A vector that overruns its reserve grows ONCE;
    // a warm-up outside the armed window swallows exactly that allocation and
    // leaves the counter reading zero forever after. Module resolution belongs
    // to setProject on the message thread, so the first block should already
    // allocate nothing - and if it ever does, that is the finding.
    // The fixture must SATURATE the bound, not merely approach it: with the
    // bound in place the collected count sits exactly on it, which is only true
    // if the clips demanded more.
    // Catch2's own macros allocate - INFO builds a std::string, and an assertion
    // that fails builds an expression - so the count has to be taken out of the
    // armed window as a plain int before any of them run. Asserting inside the
    // window measures the assertion.
    auto allocated = 0;

    {
        ScopedAllocationCount counter;

        for (int i = 0; i < 400; ++i)
            engine.processBlock (block);

        allocated = counter.count();
    }

    // CHECK, not REQUIRE: a fixture that overruns the bound must not abort
    // before the allocation count, which is the finding that matters.
    INFO ("active automations collected: " << engine.getActiveAutomationCount());
    CHECK (engine.getActiveAutomationCount() == (std::size_t) kMaxAutomations);

    INFO ("allocations inside 400 processBlock calls: " << allocated);
    CHECK (allocated == 0);

    // The other half, and the one that names the culprit when the count is
    // non-zero: a vector that reallocated has a different capacity.
    CHECK (engine.getTriggerCapacity() == triggerCapacity);
    CHECK (engine.getActiveAutomationCapacity() == automationCapacity);

    // Panic is its own window. It runs on the audio thread and it is the one
    // call that touches EVERY pooled module - modulePool.resetAll() walks all
    // of them calling reset() - so it is both the widest sweep the render path
    // makes and the rarest, which is a combination nothing else here covers.
    // A reverb that allocated in reset() would be invisible to the loop above.
    auto allocatedInPanic = 0;

    {
        ScopedAllocationCount counter;

        engine.panic();

        for (int i = 0; i < 8; ++i)
            engine.processBlock (block);

        allocatedInPanic = counter.count();
    }

    INFO ("allocations inside panic() and the eight blocks after it: " << allocatedInPanic);
    CHECK (allocatedInPanic == 0);
#endif
}

TEST_CASE ("resetting a prepared phaser allocates nothing", "[realtime][gate][allocation]")
{
#if defined(DEW_SANITIZER_OWNS_THE_ALLOCATOR)
    SKIP ("the sanitizer owns operator new; this gate runs in the ci preset");
#else
    // Why the phaser and not the other nine. Every other effect's reset() only
    // zeroes state; this one reaches juce::dsp::DryWetMixer::reset, which
    // re-sizes a FIFO and a buffer. Reading the JUCE source says both are
    // no-ops once prepare() has run - setSize is called with avoidReallocating
    // - but runChain calls reset() ON THE AUDIO THREAD whenever a pooled unit
    // changes type, so "reading the source says so" is not where this should
    // rest.
    constexpr int numSamples = 256;

    auto module = createEffectModule (EffectType::phaser);
    REQUIRE (module != nullptr);
    module->prepare (48000.0, numSamples);

    const testing::Params params { EffectType::phaser };
    const auto numParams = effectDescriptor (EffectType::phaser).numParams;

    float left[numSamples] {}, right[numSamples] {};
    auto allocated = 0;

    {
        ScopedAllocationCount counter;

        for (int i = 0; i < 32; ++i)
        {
            module->reset();
            module->process ({ params.block.data() + kNumCommonEffectParams, numParams },
                             { left, right, numSamples });
        }

        allocated = counter.count();
    }

    INFO ("allocations inside 32 reset + process pairs: " << allocated);
    CHECK (allocated == 0);
#endif
}

TEST_CASE ("the sequencer refuses at its trigger bound rather than growing",
           "[realtime][gate][allocation]")
{
    // The engine-level gate above cannot reach this bound: at a sane tempo one
    // block spans about one step, so 64 channels give 64 triggers, not 512. The
    // bound still has to hold at the extreme the schema permits - a block that
    // spans a whole pattern - so it is tested where it lives.
    constexpr auto sampleRate = 48000.0;
    constexpr auto samplesPerStep = 100.0;
    constexpr auto patternSteps = 16;

    EngineSnapshot snapshot;
    snapshot.tempoBpm = 120.0;
    snapshot.stepsPerBeat = 4;

    PatternSnapshot pattern;
    pattern.id = 1;
    pattern.lengthSteps = patternSteps;

    for (int channel = 0; channel < kMaxChannels; ++channel)
    {
        ChannelSnapshot c;
        c.id = channel + 1;
        c.mixerTrackIndex = 0;
        snapshot.channels.push_back (c);

        for (int step = 0; step < patternSteps; ++step)
        {
            NoteSnapshot note;
            note.channelIndex = channel;
            note.step = step;
            note.lengthSteps = 1;
            note.pitch = 60;
            pattern.notes.push_back (note);
        }
    }

    MixerTrackSnapshot mixer;
    mixer.id = 1;
    snapshot.mixerTracks.push_back (mixer);
    snapshot.patterns.push_back (pattern);

    // 1,024 notes are eligible; the bound is 512.
    REQUIRE ((int) pattern.notes.size() > kMaxTriggersPerBlock);

    std::vector<NoteTrigger> out;
    out.reserve ((size_t) kMaxTriggersPerBlock);

    const auto capacityBefore = out.capacity();

    // One block spanning the whole pattern.
    Sequencer::collect (
        snapshot, Transport::Mode::pattern, 0, (int) (samplesPerStep * patternSteps),
        TempoMap::constant (60.0 * sampleRate / samplesPerStep, 1), sampleRate, 0, out);

    INFO ("triggers collected: " << out.size());
    CHECK ((int) out.size() == kMaxTriggersPerBlock);
    CHECK (out.capacity() == capacityBefore);
}
