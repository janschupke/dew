#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/MidiExporter.h"
#include "engine/Sequencer.h"
#include "model/Ids.h"
#include "model/Meter.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"

using namespace dew;
using Catch::Approx;

namespace
{

/** Round-trips through memory, so a test never needs a path. */
std::unique_ptr<juce::MidiFile> roundTrip (const juce::MidiFile& file, int format)
{
    juce::MemoryOutputStream out;
    REQUIRE (file.writeTo (out, format));

    juce::MemoryInputStream in (out.getData(), out.getDataSize(), false);

    auto readBack = std::make_unique<juce::MidiFile>();
    REQUIRE (readBack->readFrom (in));

    return readBack;
}

int countNoteOns (const juce::MidiFile& file)
{
    int total = 0;

    for (int track = 0; track < file.getNumTracks(); ++track)
    {
        const auto* sequence = file.getTrack (track);

        for (int i = 0; i < sequence->getNumEvents(); ++i)
            if (sequence->getEventPointer (i)->message.isNoteOn())
                ++total;
    }

    return total;
}

/** What the sequencer itself would play over the whole material - the thing the
    exporter has to agree with.
*/
std::set<std::tuple<int, int, int>> sequencerNotes (const juce::ValueTree& project)
{
    const auto snapshot = buildSnapshot (project, nullptr);
    const auto samplesPerStep = Transport::samplesPerStepFor (snapshot.tempoBpm,
                                                              snapshot.stepsPerBeat, 44100.0);

    const auto totalSteps = Sequencer::materialLengthSteps (snapshot, Transport::Mode::song, -1);

    std::set<std::tuple<int, int, int>> notes;
    std::vector<NoteTrigger> triggers;

    // One step at a time, so every boundary lands inside a "block".
    for (int step = 0; step < totalSteps; ++step)
    {
        const auto start = (juce::int64) std::llround ((double) step * samplesPerStep);
        const auto end = (juce::int64) std::llround ((double) (step + 1) * samplesPerStep);

        Sequencer::collect (snapshot, Transport::Mode::song, start, (int) (end - start),
                            samplesPerStep, -1, triggers);

        for (const auto& trigger : triggers)
        {
            const auto& channel = snapshot.channels[(size_t) trigger.channelIndex];

            if (snapshot.isChannelAudible (channel))
                notes.insert ({ step, trigger.channelIndex, trigger.pitch });
        }
    }

    return notes;
}

} // namespace

TEST_CASE ("ticks per quarter note stay exact for every grid dew allows", "[midi][export]")
{
    for (int stepsPerBeat = 1; stepsPerBeat <= 16; ++stepsPerBeat)
    {
        const auto tpqn = MidiExporter::ticksPerQuarterNoteFor (stepsPerBeat);

        INFO ("stepsPerBeat " << stepsPerBeat << " -> " << tpqn);

        // Exact: a step is a whole number of ticks, so nothing rounds off the grid.
        REQUIRE (tpqn % stepsPerBeat == 0);

        // MidiFile stores the time format in a short.
        REQUIRE (tpqn > 0);
        REQUIRE (tpqn <= 32767);
    }

    // The conventional value survives for every common grid.
    REQUIRE (MidiExporter::ticksPerQuarterNoteFor (4) == 960);
    REQUIRE (MidiExporter::ticksPerQuarterNoteFor (3) == 960);
    REQUIRE (MidiExporter::ticksPerQuarterNoteFor (16) == 960);
}

TEST_CASE ("MIDI channel numbering skips the percussion channel", "[midi][export]")
{
    for (int i = 0; i < 40; ++i)
    {
        const auto channel = MidiExporter::midiChannelFor (i);

        REQUIRE (channel >= 1);
        REQUIRE (channel <= 16);

        // Ten is where General MIDI puts drums.
        REQUIRE (channel != 10);
    }

    REQUIRE (MidiExporter::midiChannelFor (0) == 1);
    REQUIRE (MidiExporter::midiChannelFor (9) == 11);
}

TEST_CASE ("the exported time signature is the project's", "[midi][export][meter]")
{
    auto project = ProjectFactory::createDemo();
    ProjectEdits::setMeter (project, 7, 8, nullptr);

    juce::StringArray warnings;
    juce::int64 numNotes = 0;
    const auto file = MidiExporter::build (project, {}, warnings, numNotes);
    REQUIRE (numNotes > 0);

    const auto readBack = roundTrip (file, 1);

    bool foundSignature = false;
    const auto* conductor = readBack->getTrack (0);

    for (int i = 0; i < conductor->getNumEvents(); ++i)
    {
        const auto& message = conductor->getEventPointer (i)->message;

        if (message.isTimeSignatureMetaEvent())
        {
            foundSignature = true;

            int numerator = 0, denominator = 0;
            message.getTimeSignatureInfo (numerator, denominator);

            CHECK (numerator == 7);
            CHECK (denominator == 8);
        }
    }

    CHECK (foundSignature);

    // The ticks are untouched by the denominator: a beat is still a quarter
    // note's worth of them, so the time format is what stepsPerBeat alone says.
    CHECK (readBack->getTimeFormat()
           == MidiExporter::ticksPerQuarterNoteFor ((int) project[ids::stepsPerBeat]));
}

TEST_CASE ("a metre change moves no exported note", "[midi][export][meter]")
{
    // A metre is not a tempo, and the denominator is notational, so every note
    // must land on the tick it already had. Checked with 4/4 -> 2/4, where the
    // ratio is a whole number and setMeter's rescale is exact - a metre that
    // has to round loses whole bars off the end of the arrangement, which is a
    // different claim, tested in MeterTests.
    auto project = ProjectFactory::createDemo();

    const auto onsetsOf = [] (const juce::ValueTree& tree)
    {
        juce::StringArray warnings;
        juce::int64 numNotes = 0;
        const auto file = MidiExporter::build (tree, {}, warnings, numNotes);
        REQUIRE (numNotes > 0);

        const auto readBack = roundTrip (file, 1);

        juce::Array<double> onsets;

        for (int track = 0; track < readBack->getNumTracks(); ++track)
        {
            const auto* sequence = readBack->getTrack (track);

            for (int i = 0; i < sequence->getNumEvents(); ++i)
                if (sequence->getEventPointer (i)->message.isNoteOn())
                    onsets.add (sequence->getEventPointer (i)->message.getTimeStamp());
        }

        return onsets;
    };

    const auto before = onsetsOf (project);

    auto exact = false;
    ProjectEdits::setMeter (project, 2, 4, nullptr, &exact);
    REQUIRE (exact);

    CHECK (onsetsOf (project) == before);
}

TEST_CASE ("an exported file carries the tempo and time signature", "[midi][export]")
{
    const auto project = ProjectFactory::createDemo();

    juce::StringArray warnings;
    juce::int64 numNotes = 0;
    const auto file = MidiExporter::build (project, {}, warnings, numNotes);

    REQUIRE (numNotes > 0);

    const auto readBack = roundTrip (file, 1);

    REQUIRE (readBack->getNumTracks() >= 2);
    REQUIRE (readBack->getTimeFormat()
             == MidiExporter::ticksPerQuarterNoteFor ((int) project[ids::stepsPerBeat]));

    bool foundTempo = false, foundSignature = false;

    const auto* conductor = readBack->getTrack (0);

    for (int i = 0; i < conductor->getNumEvents(); ++i)
    {
        const auto& message = conductor->getEventPointer (i)->message;

        if (message.isTempoMetaEvent())
        {
            foundTempo = true;

            // The event holds microseconds per quarter note, not BPM - the
            // easiest thing here to get backwards.
            const auto bpm = 60.0 / message.getTempoSecondsPerQuarterNote();
            REQUIRE (bpm == Approx ((double) project[ids::tempoBpm]).epsilon (0.001));
        }

        if (message.isTimeSignatureMetaEvent())
        {
            foundSignature = true;

            int numerator = 0, denominator = 0;
            message.getTimeSignatureInfo (numerator, denominator);

            const auto meter = Meter::of (project);
            REQUIRE (numerator == meter.beatsPerBar);
            REQUIRE (denominator == meter.beatUnit);
        }
    }

    REQUIRE (foundTempo);
    REQUIRE (foundSignature);
}

TEST_CASE ("the exported notes are the notes the sequencer plays", "[midi][export]")
{
    const auto project = ProjectFactory::createDemo();

    juce::StringArray warnings;
    juce::int64 numNotes = 0;
    const auto file = MidiExporter::build (project, {}, warnings, numNotes);

    const auto expected = sequencerNotes (project);

    // This is the assertion that stops the exporter and the engine drifting
    // apart: whatever Sequencer::collect would sound, the file has to contain.
    INFO ("sequencer: " << expected.size() << "  exported: " << numNotes);
    REQUIRE ((size_t) numNotes == expected.size());
    REQUIRE (countNoteOns (file) == (int) numNotes);
}

TEST_CASE ("every note off is matched to a note on", "[midi][export]")
{
    juce::StringArray warnings;
    juce::int64 numNotes = 0;
    const auto file = MidiExporter::build (ProjectFactory::createDemo(), {}, warnings, numNotes);

    const auto readBack = roundTrip (file, 1);

    for (int track = 0; track < readBack->getNumTracks(); ++track)
    {
        auto sequence = *readBack->getTrack (track);
        sequence.updateMatchedPairs();

        for (int i = 0; i < sequence.getNumEvents(); ++i)
        {
            const auto* event = sequence.getEventPointer (i);

            if (! event->message.isNoteOn())
                continue;

            REQUIRE (event->noteOffObject != nullptr);
            REQUIRE (event->noteOffObject->message.getTimeStamp() > event->message.getTimeStamp());
        }
    }
}

TEST_CASE ("a clip longer than its pattern exports the repeats", "[midi][export]")
{
    auto project = ProjectFactory::createDemo();

    auto playlist = project.getChildWithName (ids::PLAYLIST);
    REQUIRE (playlist.isValid());

    juce::ValueTree clip;

    for (const auto& track : playlist)
        for (const auto& child : track)
            if (child.hasType (ids::CLIP) && ! clip.isValid())
                clip = child;

    REQUIRE (clip.isValid());

    const auto originalBars = (int) clip[ids::lengthBars];

    juce::StringArray warnings;
    juce::int64 before = 0;
    MidiExporter::build (project, {}, warnings, before);

    clip.setProperty (ids::lengthBars, originalBars * 2, nullptr);

    juce::int64 after = 0;
    MidiExporter::build (project, {}, warnings, after);

    // Twice the bars is twice the notes, because a clip repeats its pattern.
    INFO ("before " << before << " after " << after);
    REQUIRE (after > before);
}

TEST_CASE ("a muted channel exports nothing unless asked for", "[midi][export]")
{
    auto project = ProjectFactory::createDemo();

    juce::ValueTree channel;

    for (const auto& child : project)
        if (child.hasType (ids::CHANNEL) && ! channel.isValid())
            channel = child;

    REQUIRE (channel.isValid());

    juce::StringArray warnings;
    juce::int64 unmuted = 0;
    MidiExporter::build (project, {}, warnings, unmuted);

    channel.setProperty (ids::muted, true, nullptr);

    juce::int64 muted = 0;
    MidiExporter::build (project, {}, warnings, muted);

    REQUIRE (muted < unmuted);

    MidiExportOptions includeEverything;
    includeEverything.includeMutedChannels = true;

    juce::int64 forced = 0;
    MidiExporter::build (project, includeEverything, warnings, forced);

    REQUIRE (forced == unmuted);
}

TEST_CASE ("a bar range trims the notes and rebases them to zero", "[midi][export]")
{
    const auto project = ProjectFactory::createDemo();

    juce::StringArray warnings;
    juce::int64 whole = 0;
    MidiExporter::build (project, {}, warnings, whole);

    MidiExportOptions ranged;
    ranged.barRange = { 1, 2 };

    juce::int64 partial = 0;
    const auto file = MidiExporter::build (project, ranged, warnings, partial);

    REQUIRE (partial > 0);
    REQUIRE (partial < whole);

    const auto readBack = roundTrip (file, 1);

    // Rebased: the first note has to sit inside the first bar, not at bar one.
    const auto ticksPerBar = MidiExporter::ticksPerQuarterNoteFor ((int) project[ids::stepsPerBeat])
                             * Meter::of (project).beatsPerBar;

    double earliest = 1.0e12;

    for (int track = 0; track < readBack->getNumTracks(); ++track)
    {
        const auto* sequence = readBack->getTrack (track);

        for (int i = 0; i < sequence->getNumEvents(); ++i)
            if (sequence->getEventPointer (i)->message.isNoteOn())
                earliest = juce::jmin (earliest, sequence->getEventPointer (i)->message.getTimeStamp());
    }

    REQUIRE (earliest < (double) ticksPerBar);
}

TEST_CASE ("velocity never becomes a note off", "[midi][export]")
{
    auto project = ProjectFactory::createDemo();

    // A note at zero velocity is inaudible, not absent - and MIDI velocity 0 is
    // how a note-off is spelled, so it must not round down to one.
    for (auto channel : project)
        if (channel.hasType (ids::PATTERN))
            for (auto note : channel)
                if (note.hasType (ids::NOTE))
                    note.setProperty (ids::velocity, 0.0, nullptr);

    juce::StringArray warnings;
    juce::int64 numNotes = 0;
    const auto file = MidiExporter::build (project, {}, warnings, numNotes);

    REQUIRE (numNotes > 0);

    for (int track = 0; track < file.getNumTracks(); ++track)
    {
        const auto* sequence = file.getTrack (track);

        for (int i = 0; i < sequence->getNumEvents(); ++i)
        {
            const auto& message = sequence->getEventPointer (i)->message;

            if (message.isNoteOn())
                REQUIRE (message.getVelocity() >= 1);
        }
    }
}

TEST_CASE ("format 0 is exactly one track", "[midi][export]")
{
    MidiExportOptions flat;
    flat.oneTrackPerChannel = false;

    juce::StringArray warnings;
    juce::int64 numNotes = 0;
    const auto file = MidiExporter::build (ProjectFactory::createDemo(), flat, warnings, numNotes);

    // The header records the track count, so more than one here is a malformed file.
    REQUIRE (file.getNumTracks() == 1);

    const auto readBack = roundTrip (file, 0);
    REQUIRE (readBack->getNumTracks() == 1);
    REQUIRE (countNoteOns (*readBack) == (int) numNotes);
}

TEST_CASE ("exporting MIDI writes a readable file", "[midi][export][io]")
{
    const auto target = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("dew-midi-" + juce::Uuid().toString() + ".mid");

    const auto report = MidiExporter::writeToFile (ProjectFactory::createDemo(), target);

    INFO (report.result.getErrorMessage());
    REQUIRE (report.ok());
    REQUIRE (target.existsAsFile());
    REQUIRE (report.files.size() == 1);

    juce::FileInputStream in (target);
    REQUIRE (in.openedOk());

    juce::MidiFile readBack;
    REQUIRE (readBack.readFrom (in));
    REQUIRE (countNoteOns (readBack) == (int) report.numSamples);

    target.deleteFile();
}

TEST_CASE ("a scope with no notes is refused rather than written empty", "[midi][export]")
{
    const auto target = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("dew-midi-empty-" + juce::Uuid().toString() + ".mid");

    MidiExportOptions nowhere;
    nowhere.barRange = { 900, 901 };

    const auto report = MidiExporter::writeToFile (ProjectFactory::createDemo(), target, nowhere);

    REQUIRE_FALSE (report.ok());
    REQUIRE_FALSE (target.existsAsFile());
}

TEST_CASE ("renderToFile writes MIDI when asked for it", "[engine][render][midi]")
{
    const auto target = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("dew-render-midi-" + juce::Uuid().toString() + ".mid");

    RenderOptions options;
    options.format = RenderFormat::midi;

    const auto report = OfflineRenderer::renderToFile (ProjectFactory::createDemo(), target, options);

    INFO (report.result.getErrorMessage());
    REQUIRE (report.ok());
    REQUIRE (target.existsAsFile());

    juce::FileInputStream in (target);
    juce::MidiFile readBack;
    REQUIRE (readBack.readFrom (in));
    REQUIRE (countNoteOns (readBack) > 0);

    target.deleteFile();
}
