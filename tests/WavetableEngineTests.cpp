// A wavetable slot in the engine, in automation, and across a round trip.
//
// Split out of a WavetableTests.cpp that was 1,046 lines. It already carried
// three fixture blocks, one immediately before each group of tags, so each
// file takes its own with it.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/SynthChannel.h"
#include "engine/Wavetable.h"
#include "model/AutomationTargets.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "model/ProjectSerializer.h"
#include "ui/OscillatorSection.h"

#include "FixtureProject.h"

using namespace dew;
using Catch::Approx;

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
          "basePitch": 40, "volume": 0.7, "pan": 0.0, "muted": false,
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
                               "mute": false, "effects": [] } ] }
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
