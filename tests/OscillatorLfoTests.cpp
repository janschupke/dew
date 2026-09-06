// =============================================================================
// The per-slot LFO.
//
// The first test in this file is the one the whole design rests on: an LFO that
// is asking for nothing must render the bits the engine rendered before there
// were LFOs at all. Everything else here is only worth having if that holds.
// =============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "engine/MixerBus.h"
#include "engine/SynthChannel.h"
#include "engine/EngineSnapshot.h"
#include "engine/modules/Instruments.h"
#include "model/GeneratorCatalog.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "model/ProjectSchema.h"
#include "model/ProjectSerializer.h"
#include "ui/OscillatorSection.h"

#include <algorithm>

#include "FixtureProject.h"
#include "OscillatorLfoSupport.h"

using namespace dew;
using namespace dewtest;

namespace
{

/** The same note through the INSTRUMENT, which is where the widening lives. */
juce::AudioBuffer<float> renderStereo (const OscBankSnapshot& bank, int numSamples = 8192)
{
    SynthInstrument instrument;
    instrument.prepare (44100.0, numSamples);

    const AmpSettings amp = flatAmp();

    NoteEvent on;
    on.kind = NoteEvent::Kind::on;
    on.pitch = 60;
    on.velocity = 1.0f;
    on.durationSamples = numSamples * 2;

    InstrumentContext ctx;
    ctx.events = { &on, 1 };
    ctx.osc = &bank;
    ctx.amp = &amp;

    juce::AudioBuffer<float> buffer (2, numSamples);
    buffer.clear();
    // Through the ABI, not the class: processAdd is the interface's, and driving
    // it that way is what makes this a test of the contract every instrument
    // answers to rather than of one class's access specifiers.
    InstrumentModule& module = instrument;
    module.processAdd (ctx, { buffer.getWritePointer (0), buffer.getWritePointer (1), numSamples });

    return buffer;
}

bool identical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int channelA,
                int channelB)
{
    if (a.getNumSamples() != b.getNumSamples())
        return false;

    for (int i = 0; i < a.getNumSamples(); ++i)
        if (! juce::exactlyEqual (a.getSample (channelA, i), b.getSample (channelB, i)))
            return false;

    return true;
}

bool identical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    return identical (a, b, 0, 0);
}

} // namespace

TEST_CASE ("an LFO that asks for nothing renders exactly what it always did", "[engine][lfo]")
{
    // The partition in SynthVoice::start is what this is really testing: a slot
    // that reaches the LFO pass is summed in a different order, and float
    // addition is not associative, so "no LFO" and "an LFO at zero depth" are
    // the same sound only if the second never enters that pass.
    const auto plain = renderMono (bankOf ({ classicSlot (Waveform::sine), classicSlot() }));

    SECTION ("an LFO node that is switched off")
    {
        auto off = classicSlot (Waveform::sine);
        off.lfoOn = false;
        off.lfoToPitch = 12.0f; // set, and not asking, because it is switched off
        off.lfoToPan = 1.0f;
        off.lfoActive = false;

        REQUIRE (identical (plain, renderMono (bankOf ({ off, classicSlot() }))));
    }

    SECTION ("an LFO switched ON with every depth at zero")
    {
        const auto zeroed = withLfo (classicSlot (Waveform::sine), 0.0f, 0.0f, 0.0f);

        REQUIRE_FALSE (zeroed.lfoActive);
        REQUIRE (identical (plain, renderMono (bankOf ({ zeroed, classicSlot() }))));
    }

    SECTION ("a DISABLED slot whose LFO is asking loudly")
    {
        auto disabled = withLfo (classicSlot (Waveform::triangle), 12.0f, 1.0f, 1.0f);
        disabled.enabled = false;

        REQUIRE (identical (plain, renderMono (bankOf ({ classicSlot (Waveform::sine),
                                                         classicSlot(), disabled }))));
    }
}

TEST_CASE ("the instrument widens a voice with no pan exactly as it always did", "[engine][lfo]")
{
    // The stereo path is a second, gated stage on top of the two adds
    // MonoInstrumentModule made. With nothing panned it must not run at all,
    // and both sides must be the mono render.
    const auto bank = bankOf (
        { withLfo (classicSlot (Waveform::sine), 5.0f, 0.5f, 0.0f), classicSlot() });

    const auto stereo = renderStereo (bank);
    const auto mono = renderMono (bank);

    REQUIRE (identical (stereo, stereo, 0, 1));
    REQUIRE (identical (stereo, mono, 0, 0));
}

TEST_CASE ("a modulation pan is exactly unity at its centre", "[engine][lfo]")
{
    float left = 0.0f, right = 0.0f;

    MixerBus::modulationPanGains (0.0f, left, right);

    // Exactly, not nearly. The constant-power law reads 0.7071 here, and an
    // oscillator whose LFO moves only its pitch would be attenuated by it.
    REQUIRE (juce::exactlyEqual (left, 1.0f));
    REQUIRE (juce::exactlyEqual (right, 1.0f));

    MixerBus::modulationPanGains (1.0f, left, right);
    REQUIRE (juce::exactlyEqual (left, 0.0f));
    REQUIRE (juce::exactlyEqual (right, 1.0f));

    MixerBus::modulationPanGains (-1.0f, left, right);
    REQUIRE (juce::exactlyEqual (left, 1.0f));
    REQUIRE (juce::exactlyEqual (right, 0.0f));
}

TEST_CASE ("a pan depth separates the sides, and nothing else does", "[engine][lfo]")
{
    const auto panned = renderStereo (
        bankOf ({ withLfo (classicSlot (Waveform::sine), 0.0f, 0.0f, 1.0f, 3.0f) }));

    REQUIRE_FALSE (identical (panned, panned, 0, 1));

    // Both sides still carry the note; a pan that silenced one of them would
    // pass the check above for the wrong reason.
    REQUIRE (panned.getMagnitude (0, 0, panned.getNumSamples()) > 0.01f);
    REQUIRE (panned.getMagnitude (1, 0, panned.getNumSamples()) > 0.01f);
}

TEST_CASE ("a volume depth moves the level through the note", "[engine][lfo]")
{
    const auto moving = renderMono (
        bankOf ({ withLfo (classicSlot (Waveform::sine), 0.0f, 1.0f, 0.0f, 2.0f) }), 16384);

    const auto early = moving.getMagnitude (0, 0, 2048);
    const auto late = moving.getMagnitude (0, 12288, 2048);

    REQUIRE (early > 0.001f);
    REQUIRE (! juce::exactlyEqual (early, late));

    // Attenuation only. A bipolar depth against a unity centre would otherwise
    // double the oscillator at its peak.
    const auto still = renderMono (bankOf ({ classicSlot (Waveform::sine) }), 16384);
    REQUIRE (moving.getMagnitude (0, 0, 16384) <= still.getMagnitude (0, 0, 16384) + 1.0e-6f);
}

TEST_CASE ("a pitch depth bends the sound without touching the envelope", "[engine][lfo]")
{
    const auto bent = renderInBlocks (
        bankOf ({ withLfo (classicSlot (Waveform::sine), 12.0f, 0.0f, 0.0f, 1.0f) }));
    const auto still = renderInBlocks (bankOf ({ classicSlot (Waveform::sine) }));

    REQUIRE_FALSE (identical (bent, still));

    // A pitch move is not a level move: with a flat envelope the peak is the
    // oscillator's own either way.
    REQUIRE (bent.getMagnitude (0, 0, 16384)
             == Catch::Approx (still.getMagnitude (0, 0, 16384)).margin (0.02));
}

TEST_CASE ("every note retriggers its LFO", "[engine][lfo]")
{
    // Two notes of the same pitch, one started well after the other, must move
    // identically - which is what makes a rendered arrangement reproducible
    // wherever a note happens to fall.
    const auto bank = bankOf ({ withLfo (classicSlot (Waveform::sine), 7.0f, 0.5f, 0.0f, 4.0f) });

    SynthChannel channel;
    channel.prepare (44100.0);

    juce::AudioBuffer<float> first (1, 4096), second (1, 4096);
    first.clear();
    second.clear();

    channel.noteOn (60, 1.0f, bank, flatAmp(), 8192);
    channel.renderAdd (first.getWritePointer (0), 4096);

    channel.allNotesOff();
    channel.reset();

    channel.noteOn (60, 1.0f, bank, flatAmp(), 8192);
    channel.renderAdd (second.getWritePointer (0), 4096);

    REQUIRE (identical (first, second));
}

TEST_CASE ("one slot's LFO leaves the others alone", "[engine][lfo]")
{
    // The LFO belongs to the SLOT. A second oscillator sitting beside a
    // modulated one must be the same oscillator it was.
    const auto alone = renderMono (bankOf ({ classicSlot (Waveform::sine) }));

    const auto together = renderMono (bankOf (
        { classicSlot (Waveform::sine), withLfo (classicSlot (Waveform::saw), 5.0f, 0.0f, 0.0f) }));

    const auto pair = renderMono (bankOf ({ classicSlot (Waveform::sine), classicSlot() }));

    REQUIRE_FALSE (identical (together, pair));
    REQUIRE (alone.getMagnitude (0, 0, alone.getNumSamples()) > 0.01f);
}

// -----------------------------------------------------------------------------
// The document, and the rate a synced LFO resolves to.
// -----------------------------------------------------------------------------

TEST_CASE ("a slot writes an LFO only when it has one to describe", "[schema][lfo]")
{
    // The whole reason the LFO is a node under the slot rather than eight more
    // properties on it: a project nobody has switched one on in writes exactly
    // the bytes it wrote before the feature existed.
    auto project = ProjectFactory::createDefault();
    auto channel = project.getChildWithName (ids::CHANNEL);
    auto slot = ProjectEdits::oscillatorAt (channel, 0);

    // Present in memory, which is what keeps the canonical tree one shape and
    // lets a control point at it before anybody has committed to anything.
    REQUIRE (slot.getChildWithName (ids::LFO).isValid());

    const auto fresh = ProjectSerializer::toJsonString (project);

    INFO (fresh.substring (0, 800));
    CHECK_FALSE (fresh.contains ("lfoToPitch"));
    CHECK_FALSE (fresh.contains ("\"lfo\""));

    auto lfo = generatorNodeFor (slot, ids::lfoToPitch);
    lfo.setProperty (ids::lfoToPitch, 3.5, nullptr);

    const auto dialled = ProjectSerializer::toJsonString (project);
    CHECK (dialled.contains ("lfoToPitch"));

    // And it comes back. A depth that survived the write and not the read would
    // be the same defect from the other side.
    const auto loaded = ProjectSerializer::fromJsonString (dialled);
    REQUIRE (loaded.tree.isValid());
    CHECK (loaded.warnings.isEmpty());

    auto reloaded = generatorNodeFor (
        ProjectEdits::oscillatorAt (loaded.tree.getChildWithName (ids::CHANNEL), 0),
        ids::lfoToPitch);

    CHECK ((double) reloaded[ids::lfoToPitch] == Catch::Approx (3.5));
}

TEST_CASE ("a project written before LFOs loads as one whose LFOs are off", "[schema][lfo]")
{
    // Additive with declared defaults, so there is no migration - and this is
    // what says so. A v17 slot has no `lfo` key at all.
    auto project = ProjectFactory::createDefault();
    auto json = ProjectSerializer::toJsonString (project);

    json = json.replace ("\"formatVersion\": " + juce::String (kFormatVersion),
                         "\"formatVersion\": 17");

    const auto loaded = ProjectSerializer::fromJsonString (json);

    REQUIRE (loaded.tree.isValid());
    CHECK (loaded.warnings.isEmpty());

    const auto slot = ProjectEdits::oscillatorAt (loaded.tree.getChildWithName (ids::CHANNEL), 0);
    const auto lfo = generatorNodeFor (slot, ids::lfoOn);

    REQUIRE (lfo.isValid());
    CHECK_FALSE ((bool) lfo.getProperty (ids::lfoOn, false));

    // And it is stamped forward, so the tree and the file it would be saved to
    // do not disagree.
    CHECK ((int) loaded.tree[ids::formatVersion] == kFormatVersion);
}

TEST_CASE ("an LFO parameter is written to the LFO's node, not to the slot", "[schema][lfo]")
{
    // generatorNodeFor is the one answer to "which node holds this", and every
    // control, the engine's reader and the MCP endpoint all ask it. If it
    // answered the slot, the write would land there, look right, and be dropped
    // by the schema on the next save with nothing said.
    auto project = ProjectFactory::createDefault();
    auto slot = ProjectEdits::oscillatorAt (project.getChildWithName (ids::CHANNEL), 0);

    CHECK (generatorNodeFor (slot, ids::lfoToPan).hasType (ids::LFO));
    CHECK (generatorNodeFor (slot, ids::lfoRate).hasType (ids::LFO));

    // And the slot's own five still answer the slot.
    CHECK (generatorNodeFor (slot, ids::gain).hasType (ids::OSC));
}

TEST_CASE ("a synced LFO comes round on the note value it names", "[engine][lfo]")
{
    // Read through the document, because the resolution happens in the reader:
    // the engine is handed hertz and never divides by a tempo.
    auto project = ProjectFactory::createDefault();
    project.setProperty (ids::tempoBpm, 120.0, nullptr);

    auto channel = project.getChildWithName (ids::CHANNEL);
    auto lfo = generatorNodeFor (ProjectEdits::oscillatorAt (channel, 0), ids::lfoOn);

    lfo.setProperty (ids::lfoSync, true, nullptr);
    lfo.setProperty (ids::lfoToPitch, 1.0, nullptr);

    const auto rateFor = [&] (const char* division)
    {
        lfo.setProperty (ids::lfoDivision, division, nullptr);

        return buildSnapshot (project).channels.front().osc.slots[0].lfoHz;
    };

    // A quarter note at 120bpm in 4/4 is half a second.
    CHECK (rateFor ("quarter") == Catch::Approx (2.0f));
    CHECK (rateFor ("whole") == Catch::Approx (0.5f));
    CHECK (rateFor ("eighth") == Catch::Approx (4.0f));
    CHECK (rateFor ("thirtySecond") == Catch::Approx (16.0f));
}

// -----------------------------------------------------------------------------
// The panel.
// -----------------------------------------------------------------------------

namespace
{

/** The oscillator panel, driven headlessly.

    The same shape WavetableUiTests uses: EditorState is a ChangeBroadcaster, so
    a selection only ARMS the refresh and a console test has to pump it.
*/
struct LfoPanelHarness
{
    LfoPanelHarness()
    {
        document.setState (dew::testing::fixtureProject(), true);
        section.onHeightChanged = [this] { ++heightChanges; };
        section.setSize (300, OscillatorSection::heightFor (false, /*lfoOpen*/ false));
        section.setVisible (true);
        section.setOwner (channel().getChildWithName (ids::INSTRUMENT));
        section.resized();
    }

    juce::ValueTree channel()
    {
        return document.getState().getChildWithName (ids::CHANNEL);
    }

    juce::ValueTree lfoOf (int i)
    {
        return generatorNodeFor (ProjectEdits::oscillatorAt (channel(), i), ids::lfoOn);
    }

    int heightChanges = 0;
    ProjectDocument document;
    EditorState editorState;
    OscillatorSection section { document, editorState };
};

} // namespace

TEST_CASE ("switching the LFO on opens its row and tells the host", "[ui][lfo]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    LfoPanelHarness h;

    const auto closed = h.section.getRequiredHeight();

    REQUIRE_FALSE (h.section.isShowingLfo());
    REQUIRE_FALSE (h.section.getLfoRateKnob().isVisible());

    // The header row is there whether it is on or not: a movement control
    // nothing hints at is one nobody finds.
    REQUIRE (h.section.getLfoWaveBox().isVisible());

    h.section.getLfoButton().onClick();

    REQUIRE ((bool) h.lfoOf (0)[ids::lfoOn]);
    REQUIRE (h.section.isShowingLfo());
    REQUIRE (h.section.getLfoRateKnob().isVisible());
    REQUIRE (h.section.getLfoPanKnob().isVisible());

    // And the host was told, because the LFO lives on a node it does not listen
    // to - without this the depths would be laid out past the bottom edge.
    REQUIRE (h.section.getRequiredHeight() > closed);
    REQUIRE (h.heightChanges > 0);
}

TEST_CASE ("syncing the LFO swaps the rate for a division, in the same cell", "[ui][lfo]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    LfoPanelHarness h;

    h.section.getLfoButton().onClick();

    REQUIRE (h.section.getLfoRateKnob().isVisible());
    REQUIRE_FALSE (h.section.getLfoDivisionBox().isVisible());

    const auto open = h.section.getRequiredHeight();

    h.section.getLfoSyncButton().onClick();

    REQUIRE ((bool) h.lfoOf (0)[ids::lfoSync]);
    REQUIRE_FALSE (h.section.getLfoRateKnob().isVisible());
    REQUIRE (h.section.getLfoDivisionBox().isVisible());

    // One cell, so the three depths beside it do not shift when it swaps.
    REQUIRE (h.section.getRequiredHeight() == open);
}

TEST_CASE ("every LFO parameter the catalog declares has a control", "[ui][lfo]")
{
    // The twin of the generator pairing gate, and it exists for the same
    // reason: a parameter with no control is offered by the automation picker
    // and unreachable in the panel, and a control whose property no table
    // claims would be shown for every generator, silently.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    LfoPanelHarness h;

    // The header three are controls without a row in lfoControls(), because
    // they are always on screen; everything else must pair up.
    const std::vector<const juce::Identifier*> header { &ids::lfoOn, &ids::lfoWave, &ids::lfoSync };

    juce::StringArray undrawn;

    for (const auto& spec : oscLfoParamSpecs())
    {
        const auto isHeader = std::any_of (header.begin(), header.end(),
                                           [&] (const auto* p) { return *p == *spec.property; });

        if (isHeader)
            continue;

        const auto controls = h.section.lfoControls();
        const auto drawn = std::any_of (controls.begin(), controls.end(), [&] (const auto& pair)
                                        { return *pair.first == *spec.property; });

        if (! drawn)
            undrawn.add (spec.property->toString());
    }

    INFO ("undrawn: " << undrawn.joinIntoString (", "));
    REQUIRE (undrawn.isEmpty());

    // And the other way: every control names a parameter the LFO table claims.
    for (const auto& [property, control] : h.section.lfoControls())
    {
        INFO ("control for " << property->toString());

        const auto& table = oscLfoParamSpecs();
        REQUIRE (std::any_of (table.begin(), table.end(),
                              [&] (const auto& spec) { return *spec.property == *property; }));
    }
}

TEST_CASE ("a write to the LFO node refreshes the panel", "[ui][lfo]")
{
    // The listener used to accept only the SLOT, so an undo, a preset load or
    // an MCP write to a child node moved the document and left the panel
    // showing what it used to say.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    LfoPanelHarness h;

    h.lfoOf (0).setProperty (ids::lfoOn, true, nullptr);

    REQUIRE (h.section.isShowingLfo());
    REQUIRE (h.section.getLfoPanKnob().isVisible());
}
