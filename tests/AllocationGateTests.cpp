#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdlib>
#include <new>
#include <vector>

#include "engine/AudioEngine.h"
#include "engine/EngineSnapshot.h"
#include "engine/Sequencer.h"
#include "engine/TempoMap.h"
#include "model/AutomationTargets.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"

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

std::atomic<bool> counting { false };
std::atomic<int> allocations { 0 };

inline void noteAllocation() noexcept
{
    if (counting.load (std::memory_order_relaxed))
        allocations.fetch_add (1, std::memory_order_relaxed);
}

/** Arms the counter for a scope, and always disarms - a REQUIRE that throws
    inside the window must not leave every later test counting.
*/
struct ScopedAllocationCount
{
    ScopedAllocationCount()
    {
        allocations.store (0, std::memory_order_relaxed);
        counting.store (true, std::memory_order_relaxed);
    }

    ~ScopedAllocationCount()
    {
        counting.store (false, std::memory_order_relaxed);
    }

    int count() const noexcept
    {
        return allocations.load (std::memory_order_relaxed);
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

namespace
{

/** A project built to be as hard on the render path as the schema permits.

    Deliberately past every bound rather than near it: enough channels to fill
    the rack, a note on the SAME step of every one of them so one block starts
    them all at once, and more overlapping automation clips than
    kMaxAutomations, which is the case that used to reallocate.
*/
juce::ValueTree maximalProject()
{
    auto project = ProjectFactory::createDefault();

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

        ProjectEdits::addAutomationClip (track, automationIds[(size_t) i % automationIds.size()], 0,
                                         4, nullptr);
    }

    return project;
}

} // namespace

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

    // Song mode, not pattern: collectAutomation only runs in song mode, and the
    // automation clips below are the whole point of the fixture.
    engine.setMode (Transport::Mode::song);
    engine.play();

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
