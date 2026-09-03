#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

#include "engine/SynthChannel.h"
#include "model/AutomationTargets.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "model/ProjectSerializer.h"
#include "ui/OscillatorSection.h"
#include "engine/Wavetable.h"
#include "FixtureProject.h"

using namespace dew;
using Catch::Approx;

namespace
{

/** The amplitude of one harmonic of a frame, by direct correlation.

    Deliberately not another FFT: these tests exist to pin the convention the
    generator's FFT uses, and checking it with the same transform would agree
    with itself whatever it did.
*/
double harmonicAmplitude (const Wavetable& table, float position, int mip, int k)
{
    const auto n = wavetableMipSize (mip);

    double re = 0.0, im = 0.0;

    for (int i = 0; i < n; ++i)
    {
        const auto value = (double) table.at (position, mip, (double) i / (double) n);
        const auto theta = juce::MathConstants<double>::twoPi * (double) k * (double) i
                           / (double) n;
        re += value * std::cos (theta);
        im += value * std::sin (theta);
    }

    return 2.0 * std::sqrt (re * re + im * im) / (double) n;
}

const Wavetable& tableNamed (const juce::String& name)
{
    const auto index = wavetableIndexFor (name);
    REQUIRE (index >= 0);
    return wavetableAt (index);
}

} // namespace

// --- the convention ----------------------------------------------------------

TEST_CASE ("the first frame of the basic table is a unit sine", "[wavetable]")
{
    // The test the whole generator rests on. It pins three things at once: that
    // a harmonic amplitude of 1 comes out at unit level, that bin k is a SINE
    // and not a cosine, and that the sign is positive. A saw reconstructed from
    // cosines is not a saw, and nothing downstream would say so.
    const auto& basic = tableNamed ("basic");

    for (int i = 0; i < kWavetableSize; ++i)
    {
        const auto phase = (double) i / (double) kWavetableSize;
        const auto expected = std::sin (juce::MathConstants<double>::twoPi * phase);

        INFO ("sample " << i);
        REQUIRE ((double) basic.at (0.0f, 0, phase) == Approx (expected).margin (1.0e-4));
    }
}

TEST_CASE ("the last frame of the basic table is a saw", "[wavetable]")
{
    const auto& basic = tableNamed ("basic");

    // A saw's harmonics fall as 1/k. Normalisation scales them all together, so
    // the RATIOS are what identify the shape.
    const auto first = harmonicAmplitude (basic, 1.0f, 0, 1);

    REQUIRE (first > 0.1);
    REQUIRE (harmonicAmplitude (basic, 1.0f, 0, 2) == Approx (first / 2.0).epsilon (0.05));
    REQUIRE (harmonicAmplitude (basic, 1.0f, 0, 3) == Approx (first / 3.0).epsilon (0.05));

    // Every harmonic present, which is what distinguishes a saw from a square.
    REQUIRE (harmonicAmplitude (basic, 1.0f, 0, 500) > 0.0);
}

TEST_CASE ("a mip carries the same levels as the one above it", "[wavetable]")
{
    const auto& basic = tableNamed ("basic");

    // Every mip is scaled by mip 0's peak rather than by its own. If it were
    // normalised per mip, a held note would change level as it rose through a
    // mip boundary - and this is the only place that would catch it.
    const auto reference = harmonicAmplitude (basic, 1.0f, 0, 1);

    for (int mip = 1; mip <= 5; ++mip)
    {
        INFO ("mip " << mip);
        REQUIRE (harmonicAmplitude (basic, 1.0f, mip, 1) == Approx (reference).epsilon (0.02));
    }
}

// --- the bank ----------------------------------------------------------------

TEST_CASE ("every frame of every table is finite and bounded", "[wavetable]")
{
    for (int t = 0; t < wavetableCount(); ++t)
    {
        const auto& table = wavetableAt (t);
        INFO ("table " << table.getName());

        for (int frame = 0; frame < kWavetableFrames; ++frame)
        {
            for (int mip = 0; mip < kWavetableMips; ++mip)
            {
                const auto n = wavetableMipSize (mip);
                const auto* samples = table.frameData (frame, mip);

                for (int i = 0; i <= n; ++i)
                {
                    INFO ("frame " << frame << " mip " << mip << " sample " << i);
                    REQUIRE (std::isfinite (samples[i]));

                    // Normalisation makes mip 0 peak at exactly 1; the smaller
                    // mips are allowed to overshoot it. Truncating a series
                    // rings around the shape it is approximating, and the
                    // smallest mips - four harmonics over eight samples - ring
                    // hardest. Measured worst case is 1.21, on `fold`. Clamping
                    // it away would cost a mip its level agreement with the
                    // others, which is the one thing that has to hold.
                    REQUIRE (std::abs (samples[i]) <= 1.25f);
                }

                // The guard sample is what lets interpolation skip the modulo.
                REQUIRE (juce::exactlyEqual (samples[n], samples[0]));
            }
        }
    }
}

TEST_CASE ("a frame reaches full scale", "[wavetable]")
{
    // Normalisation divides by mip 0's peak, so every frame should touch 1.
    // A frame that came out quiet would be one whose spectrum is empty.
    for (int t = 0; t < wavetableCount(); ++t)
    {
        const auto& table = wavetableAt (t);

        for (int frame = 0; frame < kWavetableFrames; ++frame)
        {
            float peak = 0.0f;
            const auto* samples = table.frameData (frame, 0);

            for (int i = 0; i < kWavetableSize; ++i)
                peak = juce::jmax (peak, std::abs (samples[i]));

            INFO (table.getName() << " frame " << frame);
            REQUIRE (peak == Approx (1.0f).margin (0.001));
        }
    }
}

TEST_CASE ("the tables differ from each other and across their frames", "[wavetable]")
{
    // A generator that ignored its frame index, or its spectrum function, would
    // still pass every test above.
    for (int t = 0; t < wavetableCount(); ++t)
    {
        const auto& table = wavetableAt (t);
        INFO (table.getName());

        bool movedAcrossFrames = false;

        for (int i = 0; i < kWavetableSize && ! movedAcrossFrames; ++i)
            movedAcrossFrames = ! juce::exactlyEqual (table.frameData (0, 0)[i],
                                                      table.frameData (kWavetableFrames - 1, 0)[i]);

        REQUIRE (movedAcrossFrames);
    }

    for (int a = 0; a < wavetableCount(); ++a)
    {
        for (int b = a + 1; b < wavetableCount(); ++b)
        {
            bool differ = false;

            for (int i = 0; i < kWavetableSize && ! differ; ++i)
                differ = ! juce::exactlyEqual (wavetableAt (a).frameData (8, 0)[i],
                                               wavetableAt (b).frameData (8, 0)[i]);

            INFO (wavetableAt (a).getName() << " vs " << wavetableAt (b).getName());
            REQUIRE (differ);
        }
    }
}

TEST_CASE ("position crossfades between neighbouring frames", "[wavetable]")
{
    const auto& basic = tableNamed ("basic");

    const auto atFrame = [&basic] (int frame, double phase)
    { return (double) basic.frameData (frame, 0)[(int) (phase * kWavetableSize)]; };

    // Landing exactly on a sample, so this measures the frame crossfade and
    // not the phase interpolation either side of it.
    const auto phase = 614.0 / (double) kWavetableSize;

    // Exactly on a frame, and exactly between two.
    REQUIRE ((double) basic.at (0.0f, 0, phase) == Approx (atFrame (0, phase)).margin (1.0e-5));
    REQUIRE ((double) basic.at (1.0f, 0, phase)
             == Approx (atFrame (kWavetableFrames - 1, phase)).margin (1.0e-5));

    const auto halfway = 0.5f / (float) (kWavetableFrames - 1);
    REQUIRE ((double) basic.at (halfway, 0, phase)
             == Approx ((atFrame (0, phase) + atFrame (1, phase)) / 2.0).margin (1.0e-5));
}

// --- the bank's names --------------------------------------------------------

TEST_CASE ("a table is addressed by name, and an unknown one is refused", "[wavetable]")
{
    REQUIRE (wavetableCount() > 0);

    for (int i = 0; i < wavetableCount(); ++i)
    {
        INFO ("index " << i);
        REQUIRE (wavetableIndexFor (wavetableAt (i).getName()) == i);
        REQUIRE (wavetableAt (i).getDisplayName().isNotEmpty());
    }

    // -1 rather than 0: a name the build does not know is a fault in the file,
    // and the caller has to be able to say so rather than silently sound wrong.
    REQUIRE (wavetableIndexFor ("nothing-like-this") == -1);

    // Out of range clamps, so no caller can index off the end of the bank.
    REQUIRE (wavetableAt (-5).getName() == wavetableAt (0).getName());
    REQUIRE (wavetableAt (9999).getName() == wavetableAt (wavetableCount() - 1).getName());
}

TEST_CASE ("the mip for a phase increment stays band-limited", "[wavetable]")
{
    REQUIRE (wavetableMipFor (0.0) == 0);
    REQUIRE (wavetableMipFor (-1.0) == 0);

    for (double dt = 1.0e-5; dt < 0.5; dt *= 1.3)
    {
        const auto mip = wavetableMipFor (dt);

        INFO ("dt " << dt << " mip " << mip);
        REQUIRE (mip >= 0);
        REQUIRE (mip < kWavetableMips);

        // The whole point: the highest harmonic this mip carries must still sit
        // below Nyquist at this pitch.
        REQUIRE ((double) wavetableMipHarmonics (mip) * dt <= 0.5 + 1.0e-9);
    }
}

// --- the engine --------------------------------------------------------------

namespace
{

OscSettings classicSlot (Waveform wave, float gain = 0.8f)
{
    OscSettings s;
    s.enabled = true;
    s.mode = OscMode::classic;
    s.wave = wave;
    s.gain = gain;
    return s;
}

OscSettings wavetableSlot (const juce::String& name = "basic", float position = 0.0f,
                           float gain = 0.8f)
{
    OscSettings s;
    s.enabled = true;
    s.mode = OscMode::wavetable;
    s.table = juce::jmax (0, wavetableIndexFor (name));
    s.position = position;
    s.gain = gain;
    return s;
}

OscBankSnapshot bankOf (std::initializer_list<OscSettings> enabledSlots)
{
    OscBankSnapshot bank;

    for (const auto& slot : enabledSlots)
        bank.slots[(size_t) bank.numSlots++] = slot;

    while (bank.numSlots < kMaxOscillators)
        bank.slots[(size_t) bank.numSlots++].enabled = false;

    bank.anyEnabled = ! std::empty (enabledSlots);
    return bank;
}

/** One note through a real channel, with no effects, no mixer and no master
    gain in the way - the same rig OscillatorTests uses, so the two files
    measure the same thing.
*/
juce::AudioBuffer<float> renderOneNote (const OscBankSnapshot& bank, int numSamples = 4096,
                                        int pitch = 60, float attack = 0.0f)
{
    SynthChannel channel;
    channel.prepare (44100.0);

    AmpSettings amp;
    amp.attack = attack;
    amp.decay = 0.0f;
    amp.sustain = 1.0f;
    amp.release = 0.0f;

    channel.noteOn (pitch, 1.0f, bank, amp, numSamples * 2);

    juce::AudioBuffer<float> buffer (1, numSamples);
    buffer.clear();
    channel.renderAdd (buffer.getWritePointer (0), numSamples);

    return buffer;
}

float peakOf (const juce::AudioBuffer<float>& buffer)
{
    return buffer.getNumSamples() > 0 ? buffer.getMagnitude (0, buffer.getNumSamples()) : 0.0f;
}

bool identical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumSamples() != b.getNumSamples())
        return false;

    for (int i = 0; i < a.getNumSamples(); ++i)
        if (! juce::exactlyEqual (a.getSample (0, i), b.getSample (0, i)))
            return false;

    return true;
}

} // namespace

TEST_CASE ("a wavetable slot leaves the classic oscillators exactly as they were",
           "[engine][wavetable]")
{
    // The assertion the whole design of the voice rests on. The wavetable
    // oscillators are summed in a second pass rather than as another case in
    // the classic switch, precisely so that a classic slot's samples are the
    // same floats in the same order they were before any of this existed.
    const auto alone = renderOneNote (bankOf ({ classicSlot (Waveform::sine) }));

    SECTION ("with a wavetable slot present but switched off")
    {
        auto bank = bankOf ({ classicSlot (Waveform::sine), wavetableSlot() });
        bank.slots[1].enabled = false;

        REQUIRE (identical (alone, renderOneNote (bank)));
    }

    SECTION ("with a wavetable slot running at no gain")
    {
        REQUIRE (
            identical (alone, renderOneNote (bankOf ({ classicSlot (Waveform::sine),
                                                       wavetableSlot ("basic", 0.0f, 0.0f) }))));
    }
}

TEST_CASE ("a wavetable slot sounds, and its position changes what it sounds like",
           "[engine][wavetable]")
{
    const auto atStart = renderOneNote (bankOf ({ wavetableSlot ("basic", 0.0f) }));
    const auto atEnd = renderOneNote (bankOf ({ wavetableSlot ("basic", 1.0f) }));

    REQUIRE (peakOf (atStart) > 0.05f);
    REQUIRE (peakOf (atEnd) > 0.05f);

    // Position 0 of `basic` is a sine and position 1 is a saw. A voice that
    // latched the position but never read it would render these the same.
    REQUIRE (! identical (atStart, atEnd));
}

TEST_CASE ("every factory table can be played", "[engine][wavetable]")
{
    for (int i = 0; i < wavetableCount(); ++i)
    {
        const auto& table = wavetableAt (i);
        INFO (table.getName());

        REQUIRE (peakOf (renderOneNote (bankOf ({ wavetableSlot (table.getName(), 0.5f) })))
                 > 0.05f);
    }
}

TEST_CASE ("position modulation moves the sound through the note", "[engine][wavetable]")
{
    auto modulated = wavetableSlot ("basic", 0.0f);
    modulated.positionMod = 0.9f;
    modulated.positionSource = PositionSource::lfo;
    modulated.positionRate = 5.0f;

    auto still = modulated;
    still.positionMod = 0.0f;

    const auto moving = renderOneNote (bankOf ({ modulated }), 16384);
    const auto fixed = renderOneNote (bankOf ({ still }), 16384);

    REQUIRE (! identical (moving, fixed));

    // The point of a wavetable: the same note does not sound the same at its
    // end as at its start. Compared as whole windows, since the pitch is
    // unchanged and only the timbre moved.
    const auto early = moving.getMagnitude (0, 2048);
    const auto late = moving.getMagnitude (12288, 2048);

    REQUIRE (early > 0.01f);
    REQUIRE (late > 0.01f);
    REQUIRE (! juce::exactlyEqual (early, late));
}

TEST_CASE ("the envelope can drive the position", "[engine][wavetable]")
{
    auto slot = wavetableSlot ("basic", 0.0f);
    slot.positionMod = 1.0f;
    slot.positionSource = PositionSource::envelope;

    // A long attack, so the envelope - and therefore the position - is still
    // climbing across the window this renders.
    const auto swept = renderOneNote (bankOf ({ slot }), 16384, 60, 0.3f);

    auto unmodulated = slot;
    unmodulated.positionMod = 0.0f;

    REQUIRE (! identical (swept, renderOneNote (bankOf ({ unmodulated }), 16384, 60, 0.3f)));
}

TEST_CASE ("unison thickens a slot without multiplying its level", "[engine][wavetable]")
{
    auto single = wavetableSlot ("basic", 1.0f);

    auto stacked = single;
    stacked.unisonVoices = 5;
    stacked.unisonDetune = 20.0f;

    const auto one = renderOneNote (bankOf ({ single }), 8192);
    const auto many = renderOneNote (bankOf ({ stacked }), 8192);

    REQUIRE (! identical (one, many));

    // Normalised WITHIN the slot: five copies are a thicker version of one
    // sound, not five times the level of it. The bound is loose because
    // detuned copies drift in and out of phase, so the peak does rise.
    REQUIRE (peakOf (many) < peakOf (one) * 2.5f);
    REQUIRE (peakOf (many) > peakOf (one) * 0.5f);
}

TEST_CASE ("one unison voice is exactly no unison at all", "[engine][wavetable]")
{
    auto single = wavetableSlot ("basic", 1.0f);

    auto explicitOne = single;
    explicitOne.unisonVoices = 1;
    explicitOne.unisonDetune = 40.0f; // nothing to spread between

    // A spread computed with a divide-by-(n-1) would produce a NaN here, and a
    // start phase of i/n would still be 0. Both have to come out untouched.
    REQUIRE (
        identical (renderOneNote (bankOf ({ single })), renderOneNote (bankOf ({ explicitOne }))));
}

TEST_CASE ("a high note does not fold its harmonics back down", "[engine][wavetable]")
{
    // The reason the table is stored as a mip pyramid at all. A saw read
    // straight from the full-band frame at this pitch would fold most of its
    // harmonics below the fundamental, where nothing but aliasing can be.
    constexpr int pitch = 108; // ~4186 Hz
    const auto fundamental = 440.0 * std::pow (2.0, (pitch - 69) / 12.0);

    const auto rendered = renderOneNote (bankOf ({ wavetableSlot ("basic", 1.0f) }), 8192, pitch);

    double belowFundamental = 0.0, total = 0.0;

    // A direct correlation per frequency, windowed, rather than an FFT: this
    // needs a handful of bins and the window is what keeps the fundamental
    // from leaking into them.
    for (double hz = 100.0; hz < fundamental * 0.6; hz += 100.0)
    {
        double re = 0.0, im = 0.0;

        for (int i = 0; i < 8192; ++i)
        {
            const auto window = 0.5
                                - 0.5
                                      * std::cos (juce::MathConstants<double>::twoPi * (double) i
                                                  / 8192.0);
            const auto value = (double) rendered.getSample (0, i) * window;
            const auto theta = juce::MathConstants<double>::twoPi * hz * (double) i / 44100.0;

            re += value * std::cos (theta);
            im += value * std::sin (theta);
        }

        belowFundamental += re * re + im * im;
    }

    for (int i = 0; i < 8192; ++i)
        total += (double) rendered.getSample (0, i) * (double) rendered.getSample (0, i);

    REQUIRE (total > 0.0);
    REQUIRE (belowFundamental / (total * 8192.0) < 0.01);
}

TEST_CASE ("a position change reaches a note that is already sounding", "[engine][wavetable]")
{
    // Position is the one oscillator setting that does not stay latched at
    // note-on. Driven at the channel, with one long note, so this measures the
    // live push and nothing else - a project-level test could not tell it apart
    // from the next note simply starting at a different position.
    const auto renderTwoWindows = [] (bool moveIt, bool pushAtAll)
    {
        SynthChannel channel;
        channel.prepare (44100.0);

        auto bank = bankOf ({ wavetableSlot ("basic", 0.0f) });

        AmpSettings amp;
        amp.attack = 0.0f;
        amp.decay = 0.0f;
        amp.sustain = 1.0f;
        amp.release = 0.0f;

        channel.noteOn (60, 1.0f, bank, amp, 1000000);

        juce::AudioBuffer<float> first (1, 2048), second (1, 2048);
        first.clear();
        second.clear();

        channel.renderAdd (first.getWritePointer (0), 2048, 0.0f, 0.0f,
                           pushAtAll ? &bank : nullptr);

        if (moveIt)
            bank.slots[0].position = 1.0f;

        channel.renderAdd (second.getWritePointer (0), 2048, 0.0f, 0.0f,
                           pushAtAll ? &bank : nullptr);

        return second;
    };

    const auto held = renderTwoWindows (false, true);
    const auto moved = renderTwoWindows (true, true);

    REQUIRE (! identical (held, moved));

    // And the other half of the contract: pushing a bank that has not moved
    // costs nothing at all. This is what makes a project with no automation
    // render bit for bit what it rendered before there was a push.
    REQUIRE (identical (held, renderTwoWindows (false, false)));
}

// --- automation --------------------------------------------------------------

TEST_CASE ("only a wavetable slot offers its position to automation", "[automation][wavetable]")
{
    auto project = dew::testing::fixtureProject();

    const auto named = [&project] (const juce::String& name)
    {
        for (const auto& target : availableAutomationTargets (project))
            if (target.displayName == name)
                return true;

        return false;
    };

    auto channel = project.getChildWithName (ids::CHANNEL);
    const auto label = channel[ids::name].toString() + " > Osc 1 > Position";

    // A classic oscillator has nothing worth drawing a curve for, and offering
    // one would be a control that silently did nothing.
    REQUIRE (! named (label));

    ProjectEdits::oscillatorAt (channel, 0).setProperty (ids::mode, "wavetable", nullptr);
    REQUIRE (named (label));

    // Switching back withdraws it again, which is what leaves an existing clip
    // inert rather than misapplied - the same thing an effect slot that changes
    // type already does.
    ProjectEdits::oscillatorAt (channel, 0).setProperty (ids::mode, "classic", nullptr);
    REQUIRE (! named (label));
}

TEST_CASE ("an automated position survives a snapshot, and a classic slot drops it",
           "[automation][wavetable][schema]")
{
    auto project = dew::testing::fixtureProject();
    juce::UndoManager undo;

    auto channel = project.getChildWithName (ids::CHANNEL);
    ProjectEdits::oscillatorAt (channel, 0).setProperty (ids::mode, "wavetable", &undo);

    AutomationTarget target;

    for (const auto& candidate : availableAutomationTargets (project))
        if (candidate.property == ids::wavePosition)
            target = candidate;

    REQUIRE (target.property == ids::wavePosition);

    auto automation = ProjectEdits::addAutomation (project, target, &undo);

    juce::StringArray warnings;
    auto snapshot = buildSnapshot (project, &warnings);

    REQUIRE (warnings.isEmpty());

    bool resolved = false;

    for (const auto& a : snapshot.automations)
        if (a.param == AutomationParam::position)
            resolved = a.scope == AutomationScope::channelOsc && a.slotIndex == 0
                       && a.targetIndex >= 0;

    REQUIRE (resolved);

    // Back to classic: the slot no longer reads a position, so the clip is
    // dropped with a warning rather than left pointing at a dead parameter.
    ProjectEdits::oscillatorAt (channel, 0).setProperty (ids::mode, "classic", &undo);

    warnings.clear();
    snapshot = buildSnapshot (project, &warnings);

    REQUIRE (! warnings.isEmpty());

    for (const auto& a : snapshot.automations)
        REQUIRE (a.param != AutomationParam::position);

    juce::ignoreUnused (automation);
}

// --- the document ------------------------------------------------------------

TEST_CASE ("a wavetable slot keeps every setting across a round trip", "[schema][wavetable]")
{
    auto project = dew::testing::fixtureProject();
    auto channel = project.getChildWithName (ids::CHANNEL);

    auto slot = ProjectEdits::oscillatorAt (channel, 1);
    slot.setProperty (ids::enabled, true, nullptr);
    slot.setProperty (ids::mode, "wavetable", nullptr);
    slot.setProperty (ids::wavetable, "formant", nullptr);
    slot.setProperty (ids::wavePosition, 0.375, nullptr);
    slot.setProperty (ids::wavePositionMod, -0.5, nullptr);
    slot.setProperty (ids::wavePositionSource, "lfo", nullptr);
    slot.setProperty (ids::wavePositionRate, 3.25, nullptr);
    slot.setProperty (ids::unisonVoices, 5, nullptr);
    slot.setProperty (ids::unisonDetune, 12.5, nullptr);

    const auto json = ProjectSerializer::toJsonString (project);
    const auto loaded = ProjectSerializer::fromJsonString (json);

    REQUIRE (loaded.ok());
    INFO ("warnings: " << loaded.warnings.joinIntoString ("; "));
    REQUIRE (loaded.warnings.isEmpty());

    const auto reloaded = ProjectEdits::oscillatorAt (loaded.tree.getChildWithName (ids::CHANNEL),
                                                      1);

    REQUIRE (reloaded[ids::mode].toString() == "wavetable");
    REQUIRE (reloaded[ids::wavetable].toString() == "formant");
    REQUIRE ((double) reloaded[ids::wavePosition] == Approx (0.375));
    REQUIRE ((double) reloaded[ids::wavePositionMod] == Approx (-0.5));
    REQUIRE (reloaded[ids::wavePositionSource].toString() == "lfo");
    REQUIRE ((double) reloaded[ids::wavePositionRate] == Approx (3.25));
    REQUIRE ((int) reloaded[ids::unisonVoices] == 5);
    REQUIRE ((double) reloaded[ids::unisonDetune] == Approx (12.5));

    // The declared default's TYPE is what the reader coerces a file value to,
    // so the types have to survive the trip: a voice count written as 5.0 would
    // hand the rounding to the schema, and a position written as 0 would stop
    // being a double the moment someone set it to a whole number.
    REQUIRE (json.contains ("\"unisonVoices\": 5"));
    REQUIRE (json.contains ("\"wavePosition\": 0.375"));
    REQUIRE (json.contains ("\"wavePositionRate\": 3.25"));
}

TEST_CASE ("a v7 project loads as classic oscillators", "[schema][compat][wavetable]")
{
    // v7 is the last format without a mode. Every oscillator in one was by
    // definition the classic generator, and nothing in the file says so - which
    // is exactly what a declared default is for. No migration should run.
    const juce::String v7 = R"({
      "format": "dew-project",
      "formatVersion": 7,
      "name": "Seven",
      "tempoBpm": 120.0,
      "stepsPerBeat": 4,
      "barsInSong": 8,
      "channels": [
        { "id": 1, "name": "Bass", "colour": "ff4fa3ff", "mixerTrackId": 1,
          "basePitch": 40, "volume": 0.7, "pan": 0.0, "muted": false, "solo": false,
          "source": "synth",
          "instrument": { "oscillators": [
                            { "enabled": true, "wave": "square", "octave": -1,
                              "detuneCents": 7.0, "gain": 0.55 } ],
                          "amp": { "attack": 0.005, "decay": 0.12,
                                   "sustain": 0.7, "release": 0.15 } },
          "effects": [] }
      ],
      "patterns": [ { "id": 1, "name": "Pattern 1", "lengthSteps": 16, "notes": [] } ],
      "automations": [],
      "playlist": { "tracks": [] },
      "mixer": { "master": { "gain": 0.9, "effects": [] },
                 "tracks": [ { "id": 1, "name": "Insert 1", "gain": 0.8, "pan": 0.0,
                               "mute": false, "solo": false, "effects": [] } ] }
    })";

    const auto loaded = ProjectSerializer::fromJsonString (v7);

    REQUIRE (loaded.ok());
    INFO ("warnings: " << loaded.warnings.joinIntoString ("; "));
    REQUIRE (loaded.warnings.isEmpty());

    const auto channel = loaded.tree.getChildWithName (ids::CHANNEL);
    const auto first = ProjectEdits::oscillatorAt (channel, 0);

    REQUIRE (first[ids::mode].toString() == "classic");
    REQUIRE (first[ids::wave].toString() == "square");
    REQUIRE (first[ids::wavetable].toString() == "basic");
    REQUIRE ((int) first[ids::unisonVoices] == 1);

    REQUIRE ((int) loaded.tree[ids::formatVersion] == kFormatVersion);
}

TEST_CASE ("a table name this build does not have is reported, not silently swapped",
           "[schema][wavetable]")
{
    auto project = dew::testing::fixtureProject();
    auto slot = ProjectEdits::oscillatorAt (project.getChildWithName (ids::CHANNEL), 0);

    slot.setProperty (ids::mode, "wavetable", nullptr);
    slot.setProperty (ids::wavetable, "something-from-the-future", nullptr);

    juce::StringArray warnings;
    const auto snapshot = buildSnapshot (project, &warnings);

    REQUIRE (warnings.joinIntoString ("; ").contains ("something-from-the-future"));
    REQUIRE (snapshot.channels.front().osc.slots[0].table == 0);

    // A CLASSIC slot never reads the table, so an unknown name there is not
    // worth a warning about a sound nobody is making.
    slot.setProperty (ids::mode, "classic", nullptr);

    warnings.clear();
    buildSnapshot (project, &warnings);

    REQUIRE (warnings.isEmpty());
}

// --- the panel ---------------------------------------------------------------

namespace
{

juce::Image renderToImage (juce::Component& c)
{
    juce::Image image (juce::Image::ARGB, juce::jmax (1, c.getWidth()),
                       juce::jmax (1, c.getHeight()), true);
    juce::Graphics g (image);
    c.paintEntireComponent (g, true);
    return image;
}

float fractionOfNonBackgroundPixels (const juce::Image& image)
{
    const auto background = image.getPixelAt (0, 0);
    int differing = 0;

    for (int y = 0; y < image.getHeight(); y += 2)
        for (int x = 0; x < image.getWidth(); x += 2)
            if (image.getPixelAt (x, y) != background)
                ++differing;

    const auto sampled = (image.getWidth() / 2) * (image.getHeight() / 2);
    return sampled > 0 ? (float) differing / (float) sampled : 0.0f;
}

struct PanelHarness
{
    PanelHarness()
    {
        document.setState (dew::testing::fixtureProject(), true);
        section.onHeightChanged = [this]
        {
            ++heightChanges;
            layOut();
        };
        section.setVisible (true);
        section.setOwner (channel().getChildWithName (ids::INSTRUMENT));
        layOut();
    }

    void layOut()
    {
        section.setSize (300, section.getRequiredHeight());
        section.resized();
    }

    /** Drives the box the way a user would, minus the message loop a console
        test does not have to deliver the change.
    */
    void choose (juce::ComboBox& box, const juce::String& itemText)
    {
        for (int i = 0; i < box.getNumItems(); ++i)
        {
            if (box.getItemText (i) != itemText)
                continue;

            box.setSelectedId (box.getItemId (i), juce::dontSendNotification);
            box.onChange();
            return;
        }

        FAIL ("no item called " << itemText);
    }

    /** Selects a slot and lets the change actually arrive.

        EditorState is a ChangeBroadcaster, so selectSlot() only ARMS the
        refresh - with no message loop in a console test nothing would ever run
        it, and every assertion after this would hold for the wrong reason.
    */
    void select (int index)
    {
        section.selectSlot (index);
        editorState.dispatchPendingMessages();
    }

    juce::ValueTree channel()
    {
        return document.getState().getChildWithName (ids::CHANNEL);
    }
    juce::ValueTree slot (int i)
    {
        return ProjectEdits::oscillatorAt (channel(), i);
    }

    int heightChanges = 0;
    ProjectDocument document;
    EditorState editorState;
    OscillatorSection section { document, editorState };
};

} // namespace

TEST_CASE ("the panel opens on the classic face", "[ui][wavetable]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PanelHarness h;

    REQUIRE (! h.section.isShowingWavetable());
    REQUIRE (h.section.getWaveBox().isVisible());
    REQUIRE (! h.section.getTableBox().isVisible());
    REQUIRE (! h.section.getPositionKnob().isVisible());
}

TEST_CASE ("choosing the wavetable mode swaps the face and the height", "[ui][wavetable]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PanelHarness h;

    const auto classicHeight = h.section.getRequiredHeight();

    h.choose (h.section.getModeBox(), "Wavetable");

    REQUIRE (h.slot (0)[ids::mode].toString() == "wavetable");
    REQUIRE (h.section.isShowingWavetable());

    // The wave picker is gone and the wavetable's own controls are up.
    REQUIRE (! h.section.getWaveBox().isVisible());
    REQUIRE (h.section.getTableBox().isVisible());
    REQUIRE (h.section.getSourceBox().isVisible());
    REQUIRE (h.section.getPositionKnob().isVisible());
    REQUIRE (h.section.getUnisonKnob().isVisible());

    // And the host was told, because the mode lives on a node it does not
    // listen to - without this the new rows would land past the bottom edge.
    REQUIRE (h.section.getRequiredHeight() > classicHeight);
    REQUIRE (h.heightChanges > 0);

    REQUIRE (OscillatorSection::heightFor (true) > OscillatorSection::heightFor (false));
}

TEST_CASE ("switching the mode is one undo step, and undoing restores the face", "[ui][wavetable]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PanelHarness h;

    h.choose (h.section.getModeBox(), "Wavetable");

    REQUIRE (h.document.getUndoManager().canUndo());

    h.document.getUndoManager().undo();

    REQUIRE (h.slot (0)[ids::mode].toString() == "classic");
    REQUIRE (! h.section.isShowingWavetable());
    REQUIRE (h.section.getWaveBox().isVisible());
}

TEST_CASE ("the wavetable controls write to the slot they are showing", "[ui][wavetable]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PanelHarness h;

    h.select (1);
    h.choose (h.section.getModeBox(), "Wavetable");
    h.choose (h.section.getTableBox(), wavetableAt (2).getDisplayName());
    h.choose (h.section.getSourceBox(), "LFO");

    h.section.getPositionKnob().setValue (0.75, juce::dontSendNotification);
    h.section.getPositionKnob().onValueChange();

    h.section.getUnisonKnob().setValue (4.0, juce::dontSendNotification);
    h.section.getUnisonKnob().onValueChange();

    REQUIRE (h.slot (1)[ids::wavetable].toString() == wavetableAt (2).getName());
    REQUIRE (h.slot (1)[ids::wavePositionSource].toString() == "lfo");
    REQUIRE ((double) h.slot (1)[ids::wavePosition] == Approx (0.75));

    // Written as an int, so the file keeps the schema default's type.
    REQUIRE (h.slot (1)[ids::unisonVoices].isInt());
    REQUIRE ((int) h.slot (1)[ids::unisonVoices] == 4);

    // The slot that is not showing is untouched.
    REQUIRE (h.slot (0)[ids::mode].toString() == "classic");
}

TEST_CASE ("selecting a slot in the other mode moves the height with it", "[ui][wavetable]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PanelHarness h;

    h.slot (1).setProperty (ids::mode, "wavetable", nullptr);

    // Slot 0 is still classic and still showing, so nothing has changed yet.
    // Also the control case: it proves the pump below is what moved things,
    // rather than the section having been in the wavetable face all along.
    REQUIRE (! h.section.isShowingWavetable());

    h.select (1);

    REQUIRE (h.section.isShowingWavetable());
    REQUIRE (h.section.getRequiredHeight() == OscillatorSection::heightFor (true));
    REQUIRE (h.heightChanges > 0);

    h.select (0);

    REQUIRE (! h.section.isShowingWavetable());
    REQUIRE (h.section.getRequiredHeight() == OscillatorSection::heightFor (false));
}

TEST_CASE ("the wavetable face paints its shape display", "[ui][wavetable]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PanelHarness h;

    const auto classicInk = fractionOfNonBackgroundPixels (renderToImage (h.section));

    h.choose (h.section.getModeBox(), "Wavetable");

    REQUIRE (fractionOfNonBackgroundPixels (renderToImage (h.section)) > 0.05f);

    // The display is drawn from the table, so moving the position has to
    // change what is on screen - a shape that never moved would be decoration.
    const auto atStart = renderToImage (h.section);

    h.section.getPositionKnob().setValue (1.0, juce::dontSendNotification);
    h.section.getPositionKnob().onValueChange();

    const auto atEnd = renderToImage (h.section);

    bool differs = false;

    for (int y = 0; y < atStart.getHeight() && ! differs; ++y)
        for (int x = 0; x < atStart.getWidth() && ! differs; ++x)
            differs = atStart.getPixelAt (x, y) != atEnd.getPixelAt (x, y);

    REQUIRE (differs);
    REQUIRE (classicInk > 0.0f);
}

TEST_CASE ("the section survives having no channel in either mode", "[ui][wavetable]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PanelHarness h;

    h.choose (h.section.getModeBox(), "Wavetable");
    h.section.setOwner ({});
    h.section.resized();

    REQUIRE (! h.section.isShowingWavetable());
    REQUIRE (h.section.getRequiredHeight() == OscillatorSection::heightFor (false));
    REQUIRE (renderToImage (h.section).isValid());
}

TEST_CASE ("the wavetable face fits the narrowest panel the app allows", "[ui][wavetable]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PanelHarness h;

    h.choose (h.section.getModeBox(), "Wavetable");

    // Settings::minPanelWidth is 220, and InstrumentPanel insets by 10 a side.
    // Three knobs share a row at that width, which is the tightest thing here.
    h.section.setSize (200, h.section.getRequiredHeight());
    h.section.resized();

    const std::initializer_list<juce::Component*> controls {
        &h.section.getTableBox(),  &h.section.getSourceBox(), &h.section.getPositionKnob(),
        &h.section.getModKnob(),   &h.section.getRateKnob(),  &h.section.getUnisonKnob(),
        &h.section.getSpreadKnob()
    };

    for (auto* control : controls)
    {
        const auto bounds = control->getBounds();

        INFO ("bounds " << bounds.toString());
        REQUIRE (bounds.getWidth() > 0);
        REQUIRE (bounds.getHeight() > 0);

        // Inside the section, not spilling off either edge or past the bottom.
        REQUIRE (h.section.getLocalBounds().contains (bounds));
    }

    // The three-knob row really is three across, not stacked or overlapping.
    REQUIRE (h.section.getPositionKnob().getRight() <= h.section.getModKnob().getX());
    REQUIRE (h.section.getModKnob().getRight() <= h.section.getRateKnob().getX());

    REQUIRE (fractionOfNonBackgroundPixels (renderToImage (h.section)) > 0.05f);
}
