#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/OfflineRenderer.h"
#include "engine/SynthChannel.h"
#include "model/Ids.h"
#include "model/ProjectDocument.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "model/ProjectSchema.h"
#include "model/ProjectSerializer.h"
#include "ui/OscillatorSection.h"

using namespace dew;
using Catch::Approx;

namespace
{

float peakOf (const juce::AudioBuffer<float>& buffer)
{
    return buffer.getNumSamples() > 0 ? buffer.getMagnitude (0, buffer.getNumSamples()) : 0.0f;
}

/** The first channel of a demo project, which is the one the render tests
    below switch oscillators on and off for.
*/
juce::ValueTree firstChannel (const juce::ValueTree& project)
{
    return project.getChildWithName (ids::CHANNEL);
}

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

/** A channel's oscillator slots, whatever built the tree. */
void requireThreeSlotsWithOnlyTheFirstOn (const juce::ValueTree& channel)
{
    REQUIRE (ProjectEdits::countOscillators (channel) == kMaxOscillators);
    REQUIRE ((bool) ProjectEdits::oscillatorAt (channel, 0)[ids::enabled] == true);

    for (int i = 1; i < kMaxOscillators; ++i)
    {
        INFO ("slot " << i);
        REQUIRE ((bool) ProjectEdits::oscillatorAt (channel, i)[ids::enabled] == false);
    }
}

} // namespace

// --- the document ------------------------------------------------------------

TEST_CASE ("a channel always carries every oscillator slot, however it was built",
           "[schema][oscillator]")
{
    SECTION ("straight from the schema's defaults")
    {
        requireThreeSlotsWithOnlyTheFirstOn (
            defaultTreeFor (childSpecFor (projectSpec(), "channels")));
    }

    SECTION ("from the factory")
    {
        requireThreeSlotsWithOnlyTheFirstOn (firstChannel (ProjectFactory::createDemo()));
    }

    SECTION ("assembled by hand, with an instrument carrying no oscillator at all")
    {
        // What a test - or a future edit - produces when it appends children
        // itself. canonicalTree is what makes it a full document again.
        juce::ValueTree channel (ids::CHANNEL);
        channel.appendChild (juce::ValueTree (ids::INSTRUMENT), nullptr);

        requireThreeSlotsWithOnlyTheFirstOn (
            canonicalTree (channel, childSpecFor (projectSpec(), "channels")));
    }

    SECTION ("through a save and a load")
    {
        const auto loaded = ProjectSerializer::fromJsonString (
            ProjectSerializer::toJsonString (ProjectFactory::createDemo()));

        REQUIRE (loaded.ok());
        requireThreeSlotsWithOnlyTheFirstOn (firstChannel (loaded.tree));
    }
}

TEST_CASE ("every oscillator keeps its own settings across a round trip",
           "[schema][oscillator]")
{
    auto project = ProjectFactory::createDemo();
    auto channel = firstChannel (project);

    auto second = ProjectEdits::oscillatorAt (channel, 1);
    second.setProperty (ids::enabled, true, nullptr);
    second.setProperty (ids::wave, "square", nullptr);
    second.setProperty (ids::octave, -1, nullptr);
    second.setProperty (ids::detuneCents, 7.0, nullptr);
    second.setProperty (ids::gain, 0.42, nullptr);

    const auto loaded = ProjectSerializer::fromJsonString (
        ProjectSerializer::toJsonString (project));

    REQUIRE (loaded.ok());
    INFO ("warnings: " << loaded.warnings.joinIntoString ("; "));
    REQUIRE (loaded.warnings.isEmpty());

    const auto reloaded = ProjectEdits::oscillatorAt (firstChannel (loaded.tree), 1);
    REQUIRE ((bool) reloaded[ids::enabled] == true);
    REQUIRE (reloaded[ids::wave].toString() == "square");
    REQUIRE ((int) reloaded[ids::octave] == -1);
    REQUIRE ((double) reloaded[ids::detuneCents] == Approx (7.0));
    REQUIRE ((double) reloaded[ids::gain] == Approx (0.42));

    // The first slot is untouched by any of that.
    const auto first = ProjectEdits::oscillatorAt (firstChannel (loaded.tree), 0);
    REQUIRE (first[ids::wave].toString() != "square");

    // detuneCents is a double in the schema, and has to stay one in the file:
    // an integer there would hand the rounding to the schema's coercion.
    REQUIRE (ProjectSerializer::toJsonString (loaded.tree).contains ("\"detuneCents\": 7.0"));
}

TEST_CASE ("a v5 project's one oscillator becomes the first slot", "[schema][compat][oscillator]")
{
    // A real v5 payload, not one this build wrote: v5 is the last format with a
    // single "osc" object, and its oscillator was by definition playing.
    const juce::String v5 = R"({
      "format": "dew-project",
      "formatVersion": 5,
      "name": "Five",
      "tempoBpm": 120.0,
      "stepsPerBeat": 4,
      "barsInSong": 8,
      "channels": [
        { "id": 1, "name": "Bass", "colour": "ff4fa3ff", "mixerTrackId": 1,
          "basePitch": 40, "volume": 0.7, "pan": 0.0, "muted": false, "solo": false,
          "instrument": { "osc": { "wave": "square", "octave": -1,
                                   "detuneCents": 7.0, "gain": 0.55 },
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

    const auto loaded = ProjectSerializer::fromJsonString (v5);

    REQUIRE (loaded.ok());

    // The assertion that proves the migration ran on the parsed JSON rather
    // than on the tree: reaching the schema with a legacy "osc" key would have
    // reported it as one the schema does not know, and dropped it.
    INFO ("warnings: " << loaded.warnings.joinIntoString ("; "));
    REQUIRE (loaded.warnings.isEmpty());

    const auto channel = firstChannel (loaded.tree);
    REQUIRE (ProjectEdits::countOscillators (channel) == kMaxOscillators);

    const auto first = ProjectEdits::oscillatorAt (channel, 0);
    REQUIRE ((bool) first[ids::enabled] == true);
    REQUIRE (first[ids::wave].toString() == "square");
    REQUIRE ((int) first[ids::octave] == -1);
    REQUIRE ((double) first[ids::detuneCents] == Approx (7.0));
    REQUIRE ((double) first[ids::gain] == Approx (0.55));

    REQUIRE ((bool) ProjectEdits::oscillatorAt (channel, 1)[ids::enabled] == false);
    REQUIRE ((bool) ProjectEdits::oscillatorAt (channel, 2)[ids::enabled] == false);

    REQUIRE ((int) loaded.tree[ids::formatVersion] == kFormatVersion);

    const auto rewritten = ProjectSerializer::toJsonString (loaded.tree);
    REQUIRE (rewritten.contains ("oscillators"));
    REQUIRE (! rewritten.contains ("\"osc\""));
}

TEST_CASE ("a file with more oscillators than the format allows is truncated and reported",
           "[schema][oscillator]")
{
    auto json = ProjectSerializer::toJsonString (ProjectFactory::createDemo());

    // Splice a fourth slot into the first channel's array. No newline in the
    // marker: JSON::toString writes CRLF, and only writeToFile narrows it.
    const auto marker = juce::String ("\"oscillators\": [");
    const auto at = json.indexOf (marker);
    REQUIRE (at >= 0);

    json = json.substring (0, at + marker.length())
           + R"({ "enabled": true, "wave": "sine", "octave": 0, "detuneCents": 0.0, "gain": 0.8 },)"
           + json.substring (at + marker.length());

    const auto loaded = ProjectSerializer::fromJsonString (json);

    REQUIRE (loaded.ok());
    REQUIRE (ProjectEdits::countOscillators (firstChannel (loaded.tree)) == kMaxOscillators);
    REQUIRE (loaded.warnings.joinIntoString ("; ").contains ("oscillators"));
}

// --- the engine --------------------------------------------------------------

namespace
{

/** One oscillator slot, as the snapshot carries it. */
OscSettings slotSettings (Waveform wave, float gain = 0.8f, int octave = 0,
                          float detuneCents = 0.0f)
{
    OscSettings s;
    s.enabled = true;
    s.wave = wave;
    s.octave = octave;
    s.detuneCents = detuneCents;
    s.gain = gain;
    return s;
}

OscBankSnapshot bankOf (std::initializer_list<OscSettings> enabledSlots)
{
    OscBankSnapshot bank;

    for (const auto& slot : enabledSlots)
        bank.slots[(size_t) bank.numSlots++] = slot;

    // The rest of the slots exist and are off, as they do in a document.
    while (bank.numSlots < kMaxOscillators)
        bank.slots[(size_t) bank.numSlots++].enabled = false;

    bank.anyEnabled = ! std::empty (enabledSlots);
    return bank;
}

/** Renders one note through a real channel, with no effects, no mixer and no
    master gain in the way - so what these tests measure is the oscillators.
*/
juce::AudioBuffer<float> renderOneNote (const OscBankSnapshot& bank, int numSamples = 4096)
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
    channel.renderAdd (buffer.getWritePointer (0), numSamples);

    return buffer;
}

} // namespace

TEST_CASE ("a second oscillator adds to the first", "[engine][oscillator]")
{
    const auto alone = renderOneNote (bankOf ({ slotSettings (Waveform::sine) }));
    const auto doubled = renderOneNote (bankOf ({ slotSettings (Waveform::sine),
                                                  slotSettings (Waveform::sine) }));

    REQUIRE (peakOf (alone) > 0.05f);

    // Summed plainly, each by its own gain: two identical oscillators are twice
    // the level, not the same level shared between them.
    REQUIRE (peakOf (doubled) == Approx (peakOf (alone) * 2.0f).margin (0.01));
}

TEST_CASE ("a switched-off oscillator contributes nothing", "[engine][oscillator]")
{
    auto withSecondOff = bankOf ({ slotSettings (Waveform::sine), slotSettings (Waveform::saw) });
    withSecondOff.slots[1].enabled = false;

    const auto alone = renderOneNote (bankOf ({ slotSettings (Waveform::sine) }));
    const auto off = renderOneNote (withSecondOff);

    REQUIRE (alone.getNumSamples() == off.getNumSamples());

    for (int i = 0; i < alone.getNumSamples(); ++i)
    {
        INFO ("sample " << i);
        REQUIRE (juce::exactlyEqual (alone.getSample (0, i), off.getSample (0, i)));
    }
}

TEST_CASE ("a voice with every oscillator off is exactly silent", "[engine][oscillator]")
{
    OscBankSnapshot bank;

    for (int i = 0; i < kMaxOscillators; ++i)
        bank.slots[(size_t) bank.numSlots++].enabled = false;

    // The voice is still allocated and its envelope still runs - there is
    // simply nothing for it to sound. Anything else would make "how many notes
    // are playing" mean "how many are playing audibly".
    REQUIRE (juce::exactlyEqual (peakOf (renderOneNote (bank)), 0.0f));
}

TEST_CASE ("each oscillator carries its own settings into the render", "[engine][oscillator]")
{
    SECTION ("its own waveform")
    {
        const auto sines = renderOneNote (bankOf ({ slotSettings (Waveform::sine),
                                                    slotSettings (Waveform::sine) }));
        const auto mixed = renderOneNote (bankOf ({ slotSettings (Waveform::sine),
                                                    slotSettings (Waveform::square) }));

        bool differs = false;

        for (int i = 0; i < sines.getNumSamples() && ! differs; ++i)
            differs = ! juce::exactlyEqual (sines.getSample (0, i), mixed.getSample (0, i));

        // A start() that latched the first slot's settings into every
        // oscillator would render these identically.
        REQUIRE (differs);
    }

    SECTION ("its own octave")
    {
        const auto unison = renderOneNote (bankOf ({ slotSettings (Waveform::sine),
                                                     slotSettings (Waveform::sine) }));
        const auto anOctaveUp = renderOneNote (
            bankOf ({ slotSettings (Waveform::sine),
                      slotSettings (Waveform::sine, 0.8f, 1) }));

        bool differs = false;

        for (int i = 0; i < unison.getNumSamples() && ! differs; ++i)
            differs = ! juce::exactlyEqual (unison.getSample (0, i),
                                            anOctaveUp.getSample (0, i));

        REQUIRE (differs);
    }

    SECTION ("its own detune, which two slots apart beat against each other")
    {
        const auto beating = renderOneNote (
            bankOf ({ slotSettings (Waveform::sine),
                      slotSettings (Waveform::sine, 0.8f, 0, 8.0f) }),
            44100);

        // Two sines eight cents apart cancel and reinforce over the beat
        // period. The quiet part is what proves they are at different pitches.
        const auto early = beating.getMagnitude (0, 2048);
        const auto quietest = beating.getMagnitude (20000, 2048);

        REQUIRE (early > 0.05f);
        REQUIRE (quietest < early);
    }

    SECTION ("its own gain, which at zero contributes nothing")
    {
        const auto alone = renderOneNote (bankOf ({ slotSettings (Waveform::sine) }));
        const auto withSilentSlot = renderOneNote (
            bankOf ({ slotSettings (Waveform::sine), slotSettings (Waveform::saw, 0.0f) }));

        for (int i = 0; i < alone.getNumSamples(); ++i)
        {
            INFO ("sample " << i);
            REQUIRE (juce::exactlyEqual (alone.getSample (0, i),
                                         withSilentSlot.getSample (0, i)));
        }
    }
}

TEST_CASE ("a channel with every oscillator switched off is silent in a real render",
           "[engine][render][oscillator]")
{
    auto project = ProjectFactory::createDemo();

    juce::AudioBuffer<float> sounding;
    REQUIRE (OfflineRenderer::renderToBuffer (project, sounding).ok());
    REQUIRE (peakOf (sounding) > 0.05f);

    for (auto channel : project)
        if (channel.hasType (ids::CHANNEL))
            for (int i = 0; i < kMaxOscillators; ++i)
                ProjectEdits::oscillatorAt (channel, i).setProperty (ids::enabled, false, nullptr);

    juce::AudioBuffer<float> silent;
    const auto report = OfflineRenderer::renderToBuffer (project, silent);

    REQUIRE (report.ok());
    REQUIRE (juce::exactlyEqual (peakOf (silent), 0.0f));
}

// --- the panel ---------------------------------------------------------------

namespace
{

struct OscHarness
{
    OscHarness()
    {
        document.setState (ProjectFactory::createDemo(), true);
        section.setSize (300, OscillatorSection::requiredHeight);
        section.setVisible (true);
        section.setOwner (channel().getChildWithName (ids::INSTRUMENT));
    }

    juce::ValueTree channel() { return firstChannel (document.getState()); }
    juce::ValueTree slot (int i) { return ProjectEdits::oscillatorAt (channel(), i); }

    ProjectDocument document;
    EditorState editorState;
    OscillatorSection section { document, editorState };
};

} // namespace

TEST_CASE ("the panel shows every oscillator slot, with only the first switched on",
           "[ui][oscillator]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    OscHarness h;

    REQUIRE (h.section.getNumSlots() == kMaxOscillators);
    REQUIRE (h.section.getSelectedSlot() == 0);
    REQUIRE (h.section.isSlotEnabled (0));
    REQUIRE (! h.section.isSlotEnabled (1));
    REQUIRE (! h.section.isSlotEnabled (2));
}

TEST_CASE ("selecting a slot changes which oscillator the controls edit", "[ui][oscillator]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    OscHarness h;

    const auto originalFirstWave = h.slot (0)[ids::wave].toString();

    h.section.selectSlot (1);
    REQUIRE (h.section.getSelectedSlot() == 1);

    // Invoked directly rather than through the widget: no message loop runs in
    // a console test, so nothing would deliver the change.
    h.section.getWaveBox().setSelectedId (3, juce::dontSendNotification);
    h.section.getWaveBox().onChange();

    REQUIRE (h.slot (1)[ids::wave].toString() == "square");
    REQUIRE (h.slot (0)[ids::wave].toString() == originalFirstWave);
}

TEST_CASE ("selecting a slot is not an edit", "[ui][oscillator]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    OscHarness h;

    h.section.selectSlot (2);

    // Which slot is showing is view state. It must not dirty the project, and
    // it must not land on the undo stack in front of the user's real edits.
    REQUIRE (! h.document.hasChangedSinceSaved());
    REQUIRE (! h.document.getUndoManager().canUndo());
}

TEST_CASE ("switching an oscillator on is one undo step", "[ui][oscillator]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    OscHarness h;

    h.section.selectSlot (1);
    h.section.getEnableButton().onClick();

    REQUIRE (h.section.isSlotEnabled (1));
    REQUIRE ((bool) h.slot (1)[ids::enabled] == true);
    REQUIRE (h.document.getUndoManager().canUndo());

    h.document.getUndoManager().undo();

    REQUIRE (! h.section.isSlotEnabled (1));
}

TEST_CASE ("switching an oscillator on leaves its settings alone", "[ui][oscillator]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    OscHarness h;

    h.section.selectSlot (1);
    h.section.getWaveBox().setSelectedId (1, juce::dontSendNotification);
    h.section.getWaveBox().onChange();

    h.section.setSlotEnabled (1, true);
    h.section.setSlotEnabled (1, false);
    h.section.setSlotEnabled (1, true);

    // A slot you switch off keeps the sound you gave it, so switching it back
    // on returns what you had rather than a fresh default.
    REQUIRE (h.slot (1)[ids::wave].toString() == "sine");
}

TEST_CASE ("the header follows every slot, the controls follow one", "[ui][oscillator]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    OscHarness h;

    const auto firstWaveId = h.section.getWaveBox().getSelectedId();

    // An edit to a slot that is not showing, made from outside the component.
    h.slot (2).setProperty (ids::enabled, true, nullptr);

    REQUIRE (h.section.isSlotEnabled (2));
    REQUIRE (h.section.getWaveBox().getSelectedId() == firstWaveId);
}

TEST_CASE ("another channel's oscillators are not this section's business", "[ui][oscillator]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    OscHarness h;

    const auto firstWaveId = h.section.getWaveBox().getSelectedId();

    auto other = ProjectEdits::findChannel (h.document.getState(), 2);
    REQUIRE (other.isValid());

    // The filter this replaced matched by node TYPE, so this edit refreshed a
    // panel showing an entirely different channel.
    auto otherOsc = ProjectEdits::oscillatorAt (other, 0);
    otherOsc.setProperty (ids::wave, otherOsc[ids::wave].toString() == "sine" ? "saw" : "sine",
                          nullptr);

    REQUIRE (h.section.getWaveBox().getSelectedId() == firstWaveId);
}

TEST_CASE ("the selected slot never leaves the slots that exist", "[ui][oscillator]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    OscHarness h;

    h.section.selectSlot (99);
    REQUIRE (h.section.getSelectedSlot() == kMaxOscillators - 1);

    h.section.selectSlot (-5);
    REQUIRE (h.section.getSelectedSlot() == 0);
}

TEST_CASE ("the section survives having no channel", "[ui][oscillator]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    OscHarness h;

    h.section.setOwner ({});
    h.section.resized();

    REQUIRE (h.section.getNumSlots() == 0);
    REQUIRE (! h.section.isSlotEnabled (0));

    // Still paints, rather than asserting its way out of an empty panel.
    REQUIRE (renderToImage (h.section).isValid());
}

TEST_CASE ("the section paints its slots and its controls", "[ui][oscillator]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    OscHarness h;

    h.section.resized();

    REQUIRE (fractionOfNonBackgroundPixels (renderToImage (h.section)) > 0.05f);
}
