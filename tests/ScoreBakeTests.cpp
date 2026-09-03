#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>

#include "io/OfflineRenderer.h"
#include "lang/Compile.h"
#include "model/Ids.h"
#include "model/Meter.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "model/ProjectSerializer.h"
#include "model/ScoreBake.h"

using namespace dew;

namespace
{

std::string exampleSource()
{
    const juce::File file { juce::String (DEW_EXAMPLES_DIR) + "/amber.score" };
    REQUIRE (file.existsAsFile());
    return file.loadFileAsString().toStdString();
}

lang::Score compileOrFail (const std::string& source)
{
    const auto result = lang::compile (source, "test.score");
    INFO (result.report (source, "test.score"));
    REQUIRE (result.ok());
    return *result.score;
}

int countChildren (const juce::ValueTree& tree, const juce::Identifier& type)
{
    auto count = 0;

    for (const auto& child : tree)
        if (child.hasType (type))
            ++count;

    return count;
}

juce::ValueTree generatedLane (const juce::ValueTree& project)
{
    for (const auto& track : project.getChildWithName (ids::PLAYLIST))
        if (track.hasType (ids::PLAYLIST_TRACK)
            && track[ids::name].toString() == ScoreBake::generatedTrackName())
            return track;

    return {};
}

/** A minimal score, so a test can vary one thing without a long fixture. */
std::string tinyScore (const std::string& body = {})
{
    return
        "song {\n"
        "  tempo 120\n"
        "  meter 4/4\n"
        "  key   C major\n"
        "}\n"
        "channel pad { mixer 1 }\n"
        "voicing warm { size 3 voices }\n"
        "rhythm held { 1/1 }\n"
        "harmony h { I | vi | IV | V }\n"
        "section verse {\n"
        "  length 4 bars\n"
        "  harmony h\n"
        "  part pad {\n"
        "    chords with warm\n"
        "    rhythm held\n"
        "  }\n"
        "}\n"
        + (body.empty() ? std::string ("arrangement {\n  verse\n}\n") : body);
}

} // namespace

TEST_CASE ("a baked score becomes ordinary patterns, notes and clips",
           "[score][bake]")
{
    const auto score = compileOrFail (tinyScore());

    BakeReport report;
    const auto project = ScoreBake::toNewProject (score, report);

    REQUIRE (report.warnings.isEmpty());
    REQUIRE (report.patternsWritten == 1);
    REQUIRE (report.clipsWritten == 1);
    REQUIRE (report.notesWritten > 0);

    // The kind guard. Every clip is one of the three dew already knows, which
    // pins the decision not to introduce a fourth - a new clip kind has to be
    // taught to four scattered places and three of them fail silently.
    auto clips = 0;

    for (const auto& track : project.getChildWithName (ids::PLAYLIST))
        for (const auto& clip : track)
            if (clip.hasType (ids::CLIP))
            {
                ++clips;
                const auto kind = clip[ids::kind].toString();
                INFO ("clip kind " << kind);
                REQUIRE ((kind == "pattern" || kind == "automation" || kind == "audio"));
            }

    REQUIRE (clips > 0);
}

TEST_CASE ("a baked project round-trips through its own file format",
           "[score][bake]")
{
    const auto score = compileOrFail (tinyScore());

    BakeReport report;
    const auto project = ScoreBake::toNewProject (score, report);

    const auto json = ProjectSerializer::toJsonString (project);
    const auto reloaded = ProjectSerializer::fromJsonString (json);

    INFO (reloaded.warnings.joinIntoString ("\n"));
    REQUIRE (reloaded.result.wasOk());
    REQUIRE (reloaded.warnings.isEmpty());

    // Diffed line by line rather than asserted as one opaque boolean: a
    // round-trip failure is almost always ONE property, and "false" tells you
    // nothing about which.
    const auto again = ProjectSerializer::toJsonString (reloaded.tree);

    juce::StringArray before, after;
    before.addLines (json);
    after.addLines (again);

    juce::String firstDifference;

    for (int i = 0; i < juce::jmax (before.size(), after.size()); ++i)
        if (before[i] != after[i])
        {
            firstDifference = "line " + juce::String (i) + "\n  wrote: " + before[i]
                            + "\n  read:  " + after[i];
            break;
        }

    INFO (firstDifference);
    REQUIRE (firstDifference.isEmpty());
    REQUIRE (reloaded.tree.isEquivalentTo (project));
}

TEST_CASE ("baking is one undo step", "[score][bake]")
{
    const auto score = compileOrFail (tinyScore());

    auto project = ProjectFactory::createDefault();
    project.setProperty (ids::stepsPerBeat, score.stepsPerBeat, nullptr);

    const auto before = project.createCopy();

    juce::UndoManager undo;
    const auto report = ScoreBake::into (project, score, &undo);

    REQUIRE (report.notesWritten > 0);
    REQUIRE_FALSE (project.isEquivalentTo (before));

    // ONE undo. beginNewTransaction ARMS a transaction rather than being a
    // no-op, so calling it per edit is what turns a bake into a hundred steps.
    REQUIRE (undo.undo());
    REQUIRE (project.isEquivalentTo (before));
}

TEST_CASE ("a track adopts a channel of the same name and leaves its sound alone",
           "[score][bake]")
{
    // The ownership line: the language owns notes, the user owns the sound.
    // A default project already has a channel called "Lead".
    const auto score = compileOrFail (
        "song {\n  tempo 120\n  meter 4/4\n  key C major\n}\n"
        "channel Lead { mixer 1 }\n"
        "voicing warm { size 3 voices }\n"
        "rhythm held { 1/1 }\n"
        "harmony h { I | V }\n"
        "section verse {\n  length 2 bars\n  harmony h\n"
        "  part Lead {\n    chords with warm\n    rhythm held\n  }\n}\n"
        "arrangement { verse }\n");

    auto project = ProjectFactory::createDefault();
    project.setProperty (ids::stepsPerBeat, score.stepsPerBeat, nullptr);

    const auto channelsBefore = countChildren (project, ids::CHANNEL);

    auto lead = ProjectEdits::findChannel (project, 4);
    REQUIRE (lead.isValid());
    REQUIRE (lead[ids::name].toString() == "Lead");

    const auto instrumentBefore = lead.getChildWithName (ids::INSTRUMENT).createCopy();

    const auto report = ScoreBake::into (project, score, nullptr);

    REQUIRE (report.channelsAdopted == 1);
    REQUIRE (report.channelsCreated == 0);

    // No second channel, and the instrument the user dialled in is untouched.
    REQUIRE (countChildren (project, ids::CHANNEL) == channelsBefore);
    REQUIRE (lead.getChildWithName (ids::INSTRUMENT).isEquivalentTo (instrumentBefore));
}

TEST_CASE ("recompiling replaces what it wrote and nothing else", "[score][bake]")
{
    const auto score = compileOrFail (tinyScore());

    auto project = ProjectFactory::createDefault();
    project.setProperty (ids::stepsPerBeat, score.stepsPerBeat, nullptr);

    // A pattern the user made, which must survive untouched.
    auto mine = ProjectEdits::addPattern (project, nullptr);
    mine.setProperty (ids::name, "Mine", nullptr);
    ProjectEdits::addNote (mine, 1, 0, 4, 60, 1.0f, nullptr);
    const auto myId = (int) mine[ids::id];

    ScoreBake::into (project, score, nullptr);
    const auto afterFirst = countChildren (project, ids::PATTERN);

    ScoreBake::into (project, score, nullptr);
    const auto afterSecond = countChildren (project, ids::PATTERN);

    // Baking twice leaves the same number of patterns, not twice as many.
    REQUIRE (afterFirst == afterSecond);

    // And the hand-made one is still there, under its own id.
    const auto survivor = ProjectEdits::findPattern (project, myId);
    REQUIRE (survivor.isValid());
    REQUIRE (survivor[ids::name].toString() == "Mine");
    REQUIRE (countChildren (survivor, ids::NOTE) == 1);

    // One generated lane, not one per bake.
    auto lanes = 0;

    for (const auto& track : project.getChildWithName (ids::PLAYLIST))
        if (track.hasType (ids::PLAYLIST_TRACK)
            && track[ids::name].toString() == ScoreBake::generatedTrackName())
            ++lanes;

    REQUIRE (lanes == 1);
}

TEST_CASE ("a grid mismatch changes nothing and says why", "[score][bake]")
{
    // stepsPerBeat owns how long a step IS, so applying the score's grid would
    // keep every existing note's step count and change how fast the whole
    // project plays. Refusing is the only safe answer.
    const auto score = compileOrFail (
        "song {\n  tempo 120\n  meter 4/4\n  key C major\n  grid 12\n}\n"
        "channel pad { mixer 1 }\n"
        "voicing warm { size 3 voices }\n"
        "rhythm held { 1/1 }\n"
        "harmony h { I | V }\n"
        "section verse {\n  length 2 bars\n  harmony h\n"
        "  part pad {\n    chords with warm\n    rhythm held\n  }\n}\n"
        "arrangement { verse }\n");

    REQUIRE (score.stepsPerBeat == 12);

    auto project = ProjectFactory::createDefault();
    REQUIRE (Meter::of (project).stepsPerBeat == 4);

    const auto before = project.createCopy();
    const auto report = ScoreBake::into (project, score, nullptr);

    REQUIRE (report.notesWritten == 0);
    REQUIRE (report.warnings.size() == 1);
    INFO (report.warnings[0]);
    REQUIRE (report.warnings[0].contains ("playback speed"));

    REQUIRE (project.isEquivalentTo (before));
}

TEST_CASE ("a meter mismatch changes nothing and says why", "[score][bake]")
{
    const auto score = compileOrFail (
        "song {\n  tempo 120\n  meter 3/4\n  key C major\n}\n"
        "channel pad { mixer 1 }\n"
        "voicing warm { size 3 voices }\n"
        "rhythm held { 1/1 }\n"
        "harmony h { I | V }\n"
        "section verse {\n  length 2 bars\n  harmony h\n"
        "  part pad {\n    chords with warm\n    rhythm held\n  }\n}\n"
        "arrangement { verse }\n");

    auto project = ProjectFactory::createDefault();
    project.setProperty (ids::stepsPerBeat, score.stepsPerBeat, nullptr);

    const auto before = project.createCopy();
    const auto report = ScoreBake::into (project, score, nullptr);

    REQUIRE (report.notesWritten == 0);
    REQUIRE (report.warnings.size() == 1);
    INFO (report.warnings[0]);
    REQUIRE (report.warnings[0].contains ("every existing clip"));

    REQUIRE (project.isEquivalentTo (before));
}

TEST_CASE ("an identical repeat is one pattern and two clips", "[score][bake]")
{
    // What a musician expects to see in a playlist, and what makes "edit it
    // once, hear it twice" work.
    const auto score = compileOrFail (tinyScore ("arrangement {\n  verse x2 identical\n}\n"));

    REQUIRE (score.patterns.size() == 1);
    REQUIRE (score.clips.size() == 2);

    BakeReport report;
    const auto project = ScoreBake::toNewProject (score, report);

    REQUIRE (report.patternsWritten == 1);
    REQUIRE (report.clipsWritten == 2);

    const auto lane = generatedLane (project);
    REQUIRE (lane.isValid());
    REQUIRE (countChildren (lane, ids::CLIP) == 2);

    // Both clips point at the same pattern.
    REQUIRE ((int) lane.getChild (0)[ids::patternId]
             == (int) lane.getChild (1)[ids::patternId]);
}

TEST_CASE ("a re-rolled repeat is two patterns", "[score][bake]")
{
    // There is no honest way to say "the same pattern, different notes" in
    // dew's document, so a repeat that varies has to become a second pattern.
    const auto score = compileOrFail (
        "song {\n  tempo 120\n  meter 4/4\n  key C major\n  seed 99\n}\n"
        "channel lead {\n  mixer 1\n  range C4..C6\n}\n"
        "rhythm pulse { 1/4 }\n"
        "harmony h { I | vi | IV | V }\n"
        "section verse {\n  length 4 bars\n  harmony h\n"
        "  part lead {\n    melody {\n      rhythm pulse\n      variance 0.9\n    }\n  }\n}\n"
        "arrangement {\n  verse x2\n}\n");

    REQUIRE (score.clips.size() == 2);
    REQUIRE (score.patterns.size() == 2);

    // And they really do differ, or the test proves nothing.
    REQUIRE (score.patterns[0].notes.size() == score.patterns[1].notes.size());

    auto differences = 0;

    for (std::size_t i = 0; i < score.patterns[0].notes.size(); ++i)
        if (score.patterns[0].notes[i].pitch != score.patterns[1].notes[i].pitch)
            ++differences;

    INFO ("differing notes " << differences);
    REQUIRE (differences > 0);
}

TEST_CASE ("the song grows to fit and never shrinks", "[score][bake]")
{
    const auto score = compileOrFail (tinyScore());

    auto project = ProjectFactory::createDefault();
    project.setProperty (ids::stepsPerBeat, score.stepsPerBeat, nullptr);
    project.setProperty (ids::barsInSong, 200, nullptr);

    ScoreBake::into (project, score, nullptr);

    // Trailing empty bars are a deliberate silence - the rule the rest of the
    // editor already follows.
    REQUIRE ((int) project[ids::barsInSong] == 200);
}

TEST_CASE ("every note the bake writes is inside its pattern", "[score][bake]")
{
    // A pattern is windowed by its clip, so a note running past the end would
    // retrigger on the next repeat rather than ring on.
    const auto score = compileOrFail (exampleSource());

    BakeReport report;
    const auto project = ScoreBake::toNewProject (score, report);

    auto checked = 0;

    for (const auto& pattern : project)
    {
        if (! pattern.hasType (ids::PATTERN))
            continue;

        const auto length = (int) pattern[ids::lengthSteps];

        for (const auto& note : pattern)
        {
            if (! note.hasType (ids::NOTE))
                continue;

            ++checked;

            const auto start = (int) note[ids::step];
            const auto end = start + (int) note[ids::lengthSteps];

            INFO ("note at " << start << " length " << (end - start)
                             << " in a pattern of " << length);
            REQUIRE (start >= 0);
            REQUIRE (end <= length);
        }
    }

    REQUIRE (checked > 100);
}

TEST_CASE ("the example score renders to real audio", "[score][bake]")
{
    // The claim that matters: text in, sound out. The same three assertions the
    // demo tests make, so this is about what it SOUNDS like rather than about
    // the shape of a tree.
    const auto score = compileOrFail (exampleSource());

    BakeReport report;
    const auto project = ScoreBake::toNewProject (score, report);

    REQUIRE (report.notesWritten > 0);

    RenderOptions options;
    options.mode = Transport::Mode::song;
    options.sampleRate = 44100.0;
    options.seconds = 8.0;

    juce::AudioBuffer<float> buffer;
    const auto rendered = OfflineRenderer::renderToBuffer (project, buffer, options);

    INFO (rendered.warnings.joinIntoString ("\n"));
    REQUIRE (buffer.getNumSamples() > 0);

    auto peak = 0.0f;
    auto sumSquares = 0.0;

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto sample = buffer.getSample (channel, i);
            peak = std::max (peak, std::abs (sample));
            sumSquares += (double) sample * sample;
        }

    const auto rms = std::sqrt (sumSquares
                                / (double) (buffer.getNumSamples()
                                            * juce::jmax (1, buffer.getNumChannels())));

    INFO ("peak " << peak << "  rms " << rms);
    REQUIRE (peak > 0.05f);
    REQUIRE (peak <= 1.0f);
    REQUIRE (rms > 0.01);
}

TEST_CASE ("the language example in the README still compiles", "[score][bake]")
{
    // The grammar has to live somewhere authoritative or it drifts within two
    // commits, and a README example that stopped compiling would be the first
    // symptom - and the last one anybody noticed. So the documentation is
    // executed rather than trusted.
    const juce::File readme { juce::File (DEW_EXAMPLES_DIR).getParentDirectory()
                                  .getChildFile ("README.md") };
    REQUIRE (readme.existsAsFile());

    const auto text = readme.loadFileAsString();

    const auto heading = text.indexOf ("## The score language");
    REQUIRE (heading > 0);

    const auto open = text.indexOf (heading, "```");
    REQUIRE (open > 0);

    const auto bodyStart = text.indexOfChar (open, '\n') + 1;
    const auto close = text.indexOf (bodyStart, "```");
    REQUIRE (close > bodyStart);

    const auto snippet = text.substring (bodyStart, close).toStdString();

    INFO (snippet);
    REQUIRE (snippet.size() > 200);

    const auto result = lang::compile (snippet, "README.md");
    INFO (result.report (snippet, "README.md"));
    REQUIRE (result.ok());

    // And it is a real example, not an empty shell that happens to parse.
    REQUIRE (result.score->noteCount() > 50);
}

TEST_CASE ("the committed example still compiles", "[score][bake]")
{
    // The example is what CI compiles and renders, so a language change that
    // breaks it fails here first, with a readable message.
    const auto source = exampleSource();
    const auto result = lang::compile (source, "amber.score");

    INFO (result.report (source, "amber.score"));
    REQUIRE (result.ok());

    const auto& score = *result.score;
    REQUIRE (score.noteCount() > 100);
    REQUIRE (score.tracks.size() == 3);
    REQUIRE (score.barsInSong > 0);
}
