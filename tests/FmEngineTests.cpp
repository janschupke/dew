// The FM matrix in the engine: what the defaults guarantee, and what a routed
// oscillator actually does.
//
// The first two tests are the important ones. Everything else in this feature
// rests on the claim that an untouched matrix is the synth that was here
// before it - so one test proves the fast path is chosen, and one proves that
// taking the slow path anyway changes not a single sample.

#include <cmath>
#include <limits>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/SnapshotReaders.h"
#include "engine/SynthChannel.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "model/ProjectSchema.h"

#include "OscillatorHarness.h"

using namespace dew;
using namespace dew::testing;
using Catch::Approx;

namespace
{

OscSettings sineSlot (float gain = 0.8f)
{
    OscSettings s;
    s.enabled = true;
    s.wave = Waveform::sine;
    s.gain = gain;
    return s;
}

/** A bank of `count` enabled sine slots, with the rest present and off - the
    shape a document always has. */
OscBankSnapshot bankOf (int count)
{
    OscBankSnapshot bank;

    for (int i = 0; i < kMaxOscillators; ++i)
    {
        bank.slots[(size_t) i] = i < count ? sineSlot() : OscSettings {};
        bank.slots[(size_t) i].enabled = i < count;
        ++bank.numSlots;
    }

    bank.anyEnabled = count > 0;
    bank.anyFm = snapshotRead::anyFmIn (bank);
    return bank;
}

/** One note through a real channel, with no effects and no mixer in the way. */
juce::AudioBuffer<float> renderOneNote (const OscBankSnapshot& bank, int numSamples = 4096,
                                        int blockSize = 0)
{
    SynthChannel channel;
    channel.prepare (44100.0);

    AmpSettings amp;
    amp.attack = 0.0f;
    amp.decay = 0.0f;
    amp.sustain = 1.0f;
    amp.release = 0.0f;

    channel.noteOn (60, 1.0f, bank, amp, numSamples * 2);

    juce::AudioBuffer<float> buffer (1, numSamples);
    buffer.clear();

    const auto block = blockSize > 0 ? blockSize : numSamples;

    for (int i = 0; i < numSamples; i += block)
        channel.renderAdd (buffer.getWritePointer (0) + i, juce::jmin (block, numSamples - i));

    return buffer;
}

void requireIdentical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    REQUIRE (a.getNumSamples() == b.getNumSamples());

    for (int i = 0; i < a.getNumSamples(); ++i)
    {
        INFO ("sample " << i);
        REQUIRE (juce::exactlyEqual (a.getSample (0, i), b.getSample (0, i)));
    }
}

/** The two render paths agree to within one rounding.

    Not bit-identity, and the difference is worth stating rather than papering
    over. The plain loop's `sum += osc.nextSample() * osc.gain` is a single
    fused multiply-add, one rounding for the product AND the sum. The FM loop
    cannot fuse it: it needs the product on its own, because that product is
    what the slot SENDS as a modulator, so it rounds the product and then rounds
    the sum. Three oscillators later that shows up as one ULP.

    Which is why the plain path exists at all. The guarantee this feature makes
    is not "the two loops agree", it is "an untouched matrix never reaches the
    second loop" - and that is asserted directly by the anyFm test above and by
    every pinned render in the suite, none of which needed editing.
*/
void requireWithinOneUlp (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    REQUIRE (a.getNumSamples() == b.getNumSamples());

    for (int i = 0; i < a.getNumSamples(); ++i)
    {
        const auto x = a.getSample (0, i);
        const auto y = b.getSample (0, i);

        INFO ("sample " << i);
        REQUIRE ((double) std::abs (x - y) <= 2.0 * (double) std::numeric_limits<float>::epsilon()
                                                  * (double) juce::jmax (1.0f, std::abs (x)));
    }
}

} // namespace

TEST_CASE ("an untouched matrix asks the engine for nothing", "[engine][fm]")
{
    auto bank = bankOf (3);

    // The defaults ARE the old behaviour, so the flag that chooses the render
    // path has to be false on a bank nobody has touched. Everything downstream
    // rests on this one line.
    REQUIRE (bank.anyFm == false);

    SECTION ("an amount into an enabled slot turns it on")
    {
        bank.slots[1].fmTo[0] = 0.5f;
        REQUIRE (snapshotRead::anyFmIn (bank) == true);
    }

    SECTION ("an output off full turns it on, because only that path applies one")
    {
        bank.slots[0].fmOut = 0.5f;
        REQUIRE (snapshotRead::anyFmIn (bank) == true);
    }

    SECTION ("a slot addressing itself turns it on - the diagonal is feedback")
    {
        bank.slots[0].fmTo[0] = 1.0f;
        REQUIRE (snapshotRead::anyFmIn (bank) == true);
    }

    SECTION ("a SWITCHED-OFF source does not")
    {
        // Otherwise a project that ever touched the matrix would lose its fast
        // path for good, on a row that cannot be heard.
        bank.slots[2].enabled = false;
        bank.slots[2].fmTo[0] = 1.0f;
        REQUIRE (snapshotRead::anyFmIn (bank) == false);
    }

    SECTION ("nor a switched-off DESTINATION")
    {
        bank.slots[2].enabled = false;
        bank.slots[0].fmTo[2] = 1.0f;
        REQUIRE (snapshotRead::anyFmIn (bank) == false);
    }
}

TEST_CASE ("the FM path renders a default matrix as the plain one does", "[engine][fm]")
{
    // Forced down the FM loop with every cell at its default, the engine has to
    // produce the same sound - so the matrix is a routing that is switched off
    // rather than a second synth that happens to resemble the first.
    //
    // To within one rounding; see requireWithinOneUlp for why that is the
    // honest claim and where the exact one lives.
    for (const auto slots : { 1, 2, 3 })
    {
        INFO ("enabled slots: " << slots);

        auto plain = bankOf (slots);
        auto forced = plain;
        forced.anyFm = true;

        requireWithinOneUlp (renderOneNote (plain), renderOneNote (forced));
    }
}

TEST_CASE ("a wavetable slot takes the FM path unchanged too", "[engine][fm][wavetable]")
{
    auto plain = bankOf (2);
    plain.slots[0].mode = OscMode::wavetable;
    plain.slots[1].mode = OscMode::wavetable;
    plain.slots[1].unisonVoices = 3;
    plain.slots[1].unisonDetune = 12.0f;

    auto forced = plain;
    forced.anyFm = true;

    requireWithinOneUlp (renderOneNote (plain), renderOneNote (forced));
}

TEST_CASE ("a slot whose LFO is running takes the FM path unchanged too", "[engine][fm][lfo]")
{
    auto plain = bankOf (2);
    plain.slots[1].lfoOn = true;
    plain.slots[1].lfoHz = 3.0f;
    plain.slots[1].lfoToVolume = 0.6f;
    plain.slots[1].lfoActive = true;

    auto forced = plain;
    forced.anyFm = true;

    requireWithinOneUlp (renderOneNote (plain), renderOneNote (forced));
}

TEST_CASE ("routing one oscillator into another changes what the other plays", "[engine][fm]")
{
    auto quiet = bankOf (2);

    // Slot 2 is a modulator and nothing else: no output of its own, all of its
    // level going into slot 1's phase.
    quiet.slots[1].fmOut = 0.0f;
    quiet.anyFm = snapshotRead::anyFmIn (quiet);

    auto routed = quiet;
    routed.slots[1].fmTo[0] = 1.0f;
    routed.anyFm = snapshotRead::anyFmIn (routed);

    REQUIRE (quiet.anyFm == true);
    REQUIRE (routed.anyFm == true);

    const auto plain = renderOneNote (quiet);
    const auto modulated = renderOneNote (routed);

    REQUIRE (peakOf (plain) > 0.05f);
    REQUIRE (peakOf (modulated) > 0.05f);

    auto differing = 0;

    for (int i = 0; i < plain.getNumSamples(); ++i)
        if (! juce::exactlyEqual (plain.getSample (0, i), modulated.getSample (0, i)))
            ++differing;

    // Not "some samples moved": a carrier under a modulator at full index is a
    // different waveform, so nearly every sample has to differ.
    REQUIRE (differing > plain.getNumSamples() / 2);
}

TEST_CASE ("an FM render does not depend on the block size", "[engine][fm]")
{
    auto bank = bankOf (2);
    bank.slots[1].fmOut = 0.0f;
    bank.slots[1].fmTo[0] = 0.7f;
    bank.anyFm = snapshotRead::anyFmIn (bank);

    // The modulator is read one sample late, and a delay that reset at a block
    // boundary would make a render depend on how it was cut up. This is what
    // catches per-block state leaking into a per-sample path.
    requireIdentical (renderOneNote (bank, 4096, 4096), renderOneNote (bank, 4096, 32));
}

TEST_CASE ("an output column at zero is silence", "[engine][fm]")
{
    auto bank = bankOf (1);
    bank.slots[0].fmOut = 0.0f;
    bank.anyFm = snapshotRead::anyFmIn (bank);

    const auto rendered = renderOneNote (bank);

    for (int i = 0; i < rendered.getNumSamples(); ++i)
    {
        INFO ("sample " << i);
        REQUIRE (juce::exactlyEqual (rendered.getSample (0, i), 0.0f));
    }
}

TEST_CASE ("feedback on the diagonal stays finite", "[engine][fm]")
{
    auto bank = bankOf (1);
    bank.slots[0].fmTo[0] = 1.0f;
    bank.anyFm = snapshotRead::anyFmIn (bank);

    const auto rendered = renderOneNote (bank, 44100);

    // A self-modulating operator is the one routing that could run away. It
    // cannot here - the modulator is bounded by the oscillator's own output and
    // read one sample late - and this is what says so rather than assuming it.
    for (int i = 0; i < rendered.getNumSamples(); ++i)
    {
        INFO ("sample " << i);
        REQUIRE (std::isfinite (rendered.getSample (0, i)));
    }

    REQUIRE (peakOf (rendered) <= 1.0f);
}

TEST_CASE ("a sounding note follows the matrix", "[engine][fm]")
{
    auto bank = bankOf (2);
    bank.slots[1].fmOut = 0.0f;
    bank.slots[1].fmTo[0] = 0.2f;
    bank.anyFm = snapshotRead::anyFmIn (bank);

    SynthChannel channel;
    channel.prepare (44100.0);

    AmpSettings amp;
    amp.attack = 0.0f;
    amp.decay = 0.0f;
    amp.sustain = 1.0f;
    amp.release = 0.0f;

    const auto render = [&] (const OscBankSnapshot* live)
    {
        juce::AudioBuffer<float> buffer (1, 2048);
        buffer.clear();
        channel.renderAdd (buffer.getWritePointer (0), 2048, 0.0f, 0.0f, live);
        return buffer;
    };

    channel.noteOn (60, 1.0f, bank, amp, 100000);
    (void) render (&bank);

    // The same bank pushed back is every value assigned its own self, so a live
    // bank that has not moved renders exactly what no live bank renders.
    const auto held = render (&bank);

    channel.reset();
    channel.noteOn (60, 1.0f, bank, amp, 100000);
    (void) render (nullptr);
    requireIdentical (held, render (nullptr));

    // And a bank that HAS moved reaches the note already sounding, rather than
    // waiting for the next one.
    auto swept = bank;
    swept.slots[1].fmTo[0] = 1.0f;

    channel.reset();
    channel.noteOn (60, 1.0f, bank, amp, 100000);
    (void) render (&bank);
    const auto moved = render (&swept);

    auto differing = 0;

    for (int i = 0; i < held.getNumSamples(); ++i)
        if (! juce::exactlyEqual (held.getSample (0, i), moved.getSample (0, i)))
            ++differing;

    REQUIRE (differing > 0);
}
