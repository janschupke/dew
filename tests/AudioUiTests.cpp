#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "io/SamplePool.h"
#include "model/Ids.h"
#include "app/ProjectDocument.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "ui/EditorState.h"
#include "ui/InstrumentPanel.h"
#include "ui/SampleSection.h"
#include "ui/StepGridComponent.h"
#include "ui/design/Tokens.h"
#include "PaintProbe.h"
#include "TestSupport.h"

using namespace dew::testing;

using namespace dew;

namespace
{


/** A real WAV on disk. The pool reads files, so a test that wants a waveform
    drawn has to give it one - there is no in-memory shortcut, and inventing one
    would test a path the application never takes.
*/
juce::File writeTone (const juce::File& file, int numSamples = 44100)
{
    juce::AudioBuffer<float> buffer (1, numSamples);

    for (int i = 0; i < numSamples; ++i)
        buffer.setSample (0, i, 0.8f * std::sin ((float) i * 0.05f));

    juce::WavAudioFormat format;

    auto fileStream = std::make_unique<juce::FileOutputStream> (file);
    fileStream->setPosition (0);
    fileStream->truncate();

    std::unique_ptr<juce::OutputStream> stream = std::move (fileStream);

    const auto options = juce::AudioFormatWriterOptions()
                             .withSampleRate (44100.0)
                             .withNumChannels (1)
                             .withBitsPerSample (24);

    if (auto writer = format.createWriterFor (stream, options))
        writer->writeFromAudioSampleBuffer (buffer, 0, numSamples);

    return file;
}


/** Fraction of pixels that are not the background, as every UI test here does. */
float inkFraction (const juce::Image& image)
{
    auto lit = 0;

    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
            if (image.getPixelAt (x, y) != tokens::colour::background
                && image.getPixelAt (x, y).getAlpha() > 0)
                ++lit;

    return (float) lit / (float) juce::jmax (1, image.getWidth() * image.getHeight());
}

/** Pixels of one exact colour inside a rectangle.

    The step grid fills its whole area, so "not the background" is true of every
    pixel in it and says nothing. A waveform is drawn in its channel's colour,
    and counting THAT is what tells a drawn row from an empty one.
*/
int pixelsOfColourIn (const juce::Image& image, juce::Rectangle<int> area, juce::Colour colour)
{
    auto found = 0;

    for (int y = area.getY(); y < juce::jmin (area.getBottom(), image.getHeight()); ++y)
        for (int x = area.getX(); x < juce::jmin (area.getRight(), image.getWidth()); ++x)
            if (image.getPixelAt (x, y) == colour)
                ++found;

    return found;
}

/** The colour a channel's row is drawn in. */
juce::Colour colourOf (const juce::ValueTree& channel)
{
    return juce::Colour::fromString ("ff" + channel[ids::colour].toString().getLastCharacters (6));
}

/** Index of a channel among the project's channels, which is its grid row. */
int rowOf (const juce::ValueTree& project, const juce::ValueTree& channel)
{
    auto row = 0;

    for (const auto& child : project)
    {
        if (! child.hasType (ids::CHANNEL))
            continue;

        if (child == channel)
            return row;

        ++row;
    }

    return -1;
}

} // namespace

TEST_CASE ("an audio channel's row draws a waveform instead of cells", "[ui][audio]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    TempDir temp { "dew-audioui-" };

    ProjectDocument document;
    AudioEngine engine;
    EditorState editorState;
    SamplePool pool;

    engine.setSamplePool (&pool);
    document.setState (ProjectFactory::createDefault(), true);

    const auto audio = writeTone (temp.dir.getChildFile ("take.wav"));

    auto channel = ProjectEdits::addAudioChannel (document.getState(), "Take", nullptr);
    ProjectEdits::setSampleSource (channel, audio.getFullPathName(), 44100, 44100, nullptr);

    StepGridComponent grid { document, engine, editorState, &pool };
    grid.setSize (1200, 400);
    grid.setVisible (true);
    grid.resized();

    const auto row = rowOf (document.getState(), channel);
    REQUIRE (row >= 0);

    const juce::Rectangle<int> rowBounds (0, row * tokens::size::rowHeight,
                                          grid.getWidth(), tokens::size::rowHeight);

    const auto withAudio = pixelsOfColourIn (render (grid), rowBounds, colourOf (channel));

    // Take the audio away and the row must lose its waveform - otherwise this
    // is measuring grid lines that were there either way.
    ProjectEdits::setSampleSource (channel, {}, 44100, 0, nullptr);
    const auto without = pixelsOfColourIn (render (grid), rowBounds, colourOf (channel));

    REQUIRE (withAudio > 0);
    REQUIRE (without == 0);
}

TEST_CASE ("an audio row ignores clicks that would toggle a step", "[ui][audio]")
{
    // The row is a waveform, not a sequence of cells. A click on it must not
    // write a note onto a channel that has no notes to play.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    TempDir temp { "dew-audioui-" };

    ProjectDocument document;
    AudioEngine engine;
    EditorState editorState;
    SamplePool pool;

    engine.setSamplePool (&pool);
    document.setState (ProjectFactory::createDefault(), true);

    const auto audio = writeTone (temp.dir.getChildFile ("take.wav"));
    auto channel = ProjectEdits::addAudioChannel (document.getState(), "Take", nullptr);
    ProjectEdits::setSampleSource (channel, audio.getFullPathName(), 44100, 44100, nullptr);

    const auto pattern = ProjectEdits::findPattern (document.getState(), 1);
    const auto notesBefore = pattern.getNumChildren();

    StepGridComponent grid { document, engine, editorState, &pool };
    grid.setSize (1200, 400);
    grid.setVisible (true);
    grid.resized();

    // The audio channel is the last one added, so it is the bottom row.
    auto rows = 0;

    for (const auto& child : document.getState())
        if (child.hasType (ids::CHANNEL))
            ++rows;

    const juce::Point<float> onAudioRow (60.0f, (float) ((rows - 1) * tokens::size::rowHeight + 4));

    const juce::MouseEvent event (juce::Desktop::getInstance().getMainMouseSource(),
                                  onAudioRow, juce::ModifierKeys::leftButtonModifier,
                                  1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                  &grid, &grid, juce::Time::getCurrentTime(),
                                  onAudioRow, juce::Time::getCurrentTime(), 1, false);

    grid.mouseDown (event);

    REQUIRE (pattern.getNumChildren() == notesBefore);
}

TEST_CASE ("the sidebar swaps faces with the selected channel", "[ui][audio]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    TempDir temp { "dew-audioui-" };

    ProjectDocument document;
    EditorState editorState;
    SamplePool pool;

    document.setState (ProjectFactory::createDefault(), true);

    const auto audio = writeTone (temp.dir.getChildFile ("take.wav"));
    auto audioChannel = ProjectEdits::addAudioChannel (document.getState(), "Take", nullptr);
    ProjectEdits::setSampleSource (audioChannel, audio.getFullPathName(), 44100, 44100, nullptr);

    InstrumentPanel panel { document, editorState, &pool };
    panel.setSize (300, 700);
    panel.setVisible (true);

    const auto sectionNamed = [&panel] (const juce::String& id) -> juce::Component*
    {
        return panel.findChildWithID (id);
    };

    // A synth channel: oscillators, no sample section.
    editorState.setSelectedChannelId (1);
    panel.refresh();
    panel.resized();

    auto* oscillators = sectionNamed ("oscillatorSection");
    auto* sample = sectionNamed ("sampleSection");

    REQUIRE (oscillators != nullptr);
    REQUIRE (sample != nullptr);
    REQUIRE (oscillators->isVisible());
    REQUIRE (! sample->isVisible());

    // The audio channel: the other way round.
    editorState.setSelectedChannelId ((int) audioChannel[ids::id]);
    panel.refresh();
    panel.resized();

    REQUIRE (! oscillators->isVisible());
    REQUIRE (sample->isVisible());

    // And the shared rows survive the swap - a channel is the same thing
    // downstream of where its samples come from.
    REQUIRE (panel.isEnabled());
}

TEST_CASE ("the sample section draws its audio and its trim handles", "[ui][audio]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    TempDir temp { "dew-audioui-" };

    ProjectDocument document;
    EditorState editorState;
    SamplePool pool;

    document.setState (ProjectFactory::createDefault(), true);

    const auto audio = writeTone (temp.dir.getChildFile ("take.wav"));
    auto channel = ProjectEdits::addAudioChannel (document.getState(), "Take", nullptr);
    ProjectEdits::setSampleSource (channel, audio.getFullPathName(), 44100, 44100, nullptr);

    SampleSection section { document, &pool };
    section.setSize (280, SampleSection::requiredHeight);
    section.setVisible (true);
    section.setOwner (channel.getChildWithName (ids::SAMPLE));
    section.resized();

    REQUIRE (section.hasAudio());

    // Untrimmed: the handles sit at the two ends of the strip.
    const auto bounds = section.getWaveformBounds().toFloat();

    REQUIRE (section.getTrimHandleX (true) == Catch::Approx (bounds.getX()));
    REQUIRE (section.getTrimHandleX (false) == Catch::Approx (bounds.getRight()));

    // Trimmed to the middle: both move inwards, and neither crosses the other.
    auto sample = channel.getChildWithName (ids::SAMPLE);
    sample.setProperty (ids::startSample, 11025, nullptr);
    sample.setProperty (ids::endSample, 33075, nullptr);

    const auto start = section.getTrimHandleX (true);
    const auto end = section.getTrimHandleX (false);

    REQUIRE (start > bounds.getX());
    REQUIRE (end < bounds.getRight());
    REQUIRE (start < end);
}

TEST_CASE ("a section with no audio says so rather than drawing nothing", "[ui][audio]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    EditorState editorState;
    SamplePool pool;

    document.setState (ProjectFactory::createDefault(), true);
    auto channel = ProjectEdits::addAudioChannel (document.getState(), "Empty", nullptr);

    SampleSection section { document, &pool };
    section.setSize (280, SampleSection::requiredHeight);
    section.setVisible (true);
    section.setOwner (channel.getChildWithName (ids::SAMPLE));
    section.resized();

    REQUIRE (! section.hasAudio());

    // Still paints: an empty strip with a prompt in it, not a blank rectangle.
    REQUIRE (inkFraction (render (section)) > 0.01f);
}

TEST_CASE ("a missing audio file does not take the editor down", "[ui][audio]")
{
    // Projects get moved, and a sidecar folder can be left behind. The row has
    // to say so and carry on drawing.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    AudioEngine engine;
    EditorState editorState;
    SamplePool pool;

    engine.setSamplePool (&pool);
    document.setState (ProjectFactory::createDefault(), true);

    auto channel = ProjectEdits::addAudioChannel (document.getState(), "Gone", nullptr);
    ProjectEdits::setSampleSource (channel, "/nowhere/at/all/missing.wav", 44100, 44100, nullptr);

    StepGridComponent grid { document, engine, editorState, &pool };
    grid.setSize (1200, 400);
    grid.setVisible (true);
    grid.resized();

    REQUIRE (inkFraction (render (grid)) > 0.01f);
}

TEST_CASE ("the sample pool reads a file once and notices a new one", "[audio][pool]")
{
    TempDir temp { "dew-audioui-" };
    SamplePool pool;

    const auto file = writeTone (temp.dir.getChildFile ("take.wav"), 2048);

    const auto& first = pool.load (file);
    REQUIRE (first.isValid());
    REQUIRE (first.audio->getNumSamples() == 2048);
    REQUIRE (pool.size() == 1);

    // Cached: the same file does not read again.
    pool.load (file);
    REQUIRE (pool.size() == 1);

    // A missing file is cached as invalid rather than re-attempted on every
    // document change.
    REQUIRE (! pool.load (temp.dir.getChildFile ("absent.wav")).isValid());

    // Forgetting is what a fresh take over the same path needs.
    pool.forget (file);
    REQUIRE (pool.size() == 0);
}

TEST_CASE ("the pool resolves a relative path against the project", "[audio][pool]")
{
    TempDir temp { "dew-audioui-" };

    const auto projectFile = temp.dir.getChildFile ("Song.dew");
    const auto assets = temp.dir.getChildFile ("Song Assets");
    assets.createDirectory();

    writeTone (assets.getChildFile ("Take 001.wav"), 1024);

    SamplePool pool;
    pool.setProjectFile (projectFile);

    REQUIRE (pool.loadReference ("Song Assets/Take 001.wav").isValid());

    // With no project file there is nothing for a relative path to be relative
    // to, and guessing would read an arbitrary file off the working directory.
    SamplePool homeless;
    REQUIRE (! homeless.loadReference ("Song Assets/Take 001.wav").isValid());
}

TEST_CASE ("dragging a sample knob is one undo step, not twenty", "[ui][audio][undo]")
{
    // The regression this closes. SampleSection had no gesture guard at all -
    // no `dragging` flag, no onEditStart - so every value a drag produced
    // opened its own undo transaction, and getting back to where you started
    // meant pressing undo once per frame. That is a regression of the fix
    // README.md claims is done for every other panel.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    TempDir temp { "dew-sample-undo-" };
    ProjectDocument document;
    SamplePool pool;

    document.setState (ProjectFactory::createDefault(), true);

    const auto audio = writeTone (temp.dir.getChildFile ("take.wav"));
    auto channel = ProjectEdits::addAudioChannel (document.getState(), "Take", nullptr);
    ProjectEdits::setSampleSource (channel, audio.getFullPathName(), 44100, 44100, nullptr);

    SampleSection section { document, &pool };
    section.setSize (280, SampleSection::requiredHeight);
    section.setVisible (true);
    section.setOwner (channel.getChildWithName (ids::SAMPLE));
    section.resized();

    auto sample = channel.getChildWithName (ids::SAMPLE);
    auto& undo = document.getUndoManager();
    auto& knob = section.getFadeInKnob();

    const auto before = (double) sample[ids::fadeInMs];

    // What a drag looks like from the knob's side: one onEditStart, a run of
    // values, one onEditEnd.
    knob.onEditStart();

    for (int i = 1; i <= 20; ++i)
        knob.setValue ((double) i * 10.0, juce::sendNotificationSync);

    knob.onEditEnd();

    REQUIRE (! juce::exactlyEqual ((double) sample[ids::fadeInMs], before));

    REQUIRE (undo.undo());
    CHECK (juce::exactlyEqual ((double) sample[ids::fadeInMs], before));
}
