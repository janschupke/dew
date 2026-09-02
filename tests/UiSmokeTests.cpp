#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/LiveAudioHost.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "ui/MainComponent.h"

namespace
{

/** Paints a component into an offscreen image.

    This is how the UI is verified in CI and on machines where screen-recording
    permission is not granted: no display, no window server, just the paint
    path. A component that silently failed to lay out paints a flat fill, which
    these tests can tell apart from one that drew something.
*/
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

} // namespace

TEST_CASE ("the editor lays out and paints", "[ui][smoke]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    dew::MainComponent component;

    REQUIRE (component.getWidth() > 0);
    REQUIRE (component.getHeight() > 0);

    const auto image = renderToImage (component);
    REQUIRE (image.isValid());

    const auto content = fractionOfNonBackgroundPixels (image);
    INFO ("non-background pixel fraction: " << content);
    REQUIRE (content > 0.01f);
}

TEST_CASE ("the editor survives being resized to its limits", "[ui][smoke]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    dew::MainComponent component;

    for (auto size : { juce::Point<int> { 900, 560 }, juce::Point<int> { 2400, 1400 } })
    {
        component.setSize (size.x, size.y);
        const auto image = renderToImage (component);

        REQUIRE (image.getWidth() == size.x);
        REQUIRE (image.getHeight() == size.y);
        REQUIRE (fractionOfNonBackgroundPixels (image) > 0.01f);
    }
}

TEST_CASE ("every editor tab paints something", "[ui][smoke]")
{
    // A tab that throws or lays out to nothing would otherwise only be found by
    // a person clicking on it.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    dew::MainComponent component;
    component.getDocument().setState (dew::ProjectFactory::createDemo(), true);
    component.documentWasReplaced();
    component.setSize (1280, 800);

    juce::TabbedComponent* tabs = nullptr;

    for (auto* child : component.getChildren())
        if (auto* asTabs = dynamic_cast<juce::TabbedComponent*> (child))
            tabs = asTabs;

    REQUIRE (tabs != nullptr);
    REQUIRE (tabs->getNumTabs() == 4);

    for (int i = 0; i < tabs->getNumTabs(); ++i)
    {
        tabs->setCurrentTabIndex (i, true);
        component.resized();

        const auto image = renderToImage (component);

        INFO ("tab " << i << " (" << tabs->getTabNames()[i] << ")");
        REQUIRE (fractionOfNonBackgroundPixels (image) > 0.01f);
    }
}

TEST_CASE ("the editor's own engine renders audio for the loaded project", "[ui][smoke][audio]")
{
    // Not the offline renderer: this is the engine MainComponent actually owns,
    // fed by the document it actually holds. It is the wiring between the two
    // that a person clicking Play would exercise.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    dew::MainComponent component;
    component.getDocument().setState (dew::ProjectFactory::createDemo(), true);
    component.documentWasReplaced();

    auto& engine = component.getEngine();
    engine.prepare (44100.0, 512);
    engine.setMode (dew::Transport::Mode::song);
    engine.rewind();
    engine.play();

    juce::AudioBuffer<float> block (2, 512);
    float peak = 0.0f;

    // Two seconds is enough to pass several notes of the demo.
    for (int i = 0; i < 172; ++i)
    {
        engine.processBlock (block);
        peak = juce::jmax (peak, block.getMagnitude (0, block.getNumSamples()));
    }

    INFO ("peak from the editor's engine: " << peak);
    REQUIRE (peak > 0.01f);
}

TEST_CASE ("editing through the UI's edit API changes what the engine plays", "[ui][smoke][audio]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    dew::MainComponent component;
    auto& document = component.getDocument();

    // A new project is silent: channels but no notes.
    document.setState (dew::ProjectFactory::createDefault(), true);
    component.documentWasReplaced();

    auto& engine = component.getEngine();
    engine.prepare (44100.0, 512);
    engine.setMode (dew::Transport::Mode::pattern);
    engine.setCurrentPatternId (1);
    engine.rewind();
    engine.play();

    juce::AudioBuffer<float> block (2, 512);

    const auto renderPeak = [&]
    {
        float peak = 0.0f;

        for (int i = 0; i < 172; ++i)
        {
            engine.processBlock (block);
            peak = juce::jmax (peak, block.getMagnitude (0, block.getNumSamples()));
        }

        return peak;
    };

    REQUIRE (juce::exactlyEqual (renderPeak(), 0.0f));

    // Light a step the way the step grid does.
    auto pattern = dew::ProjectEdits::findPattern (document.getState(), 1);
    auto channel = dew::ProjectEdits::findChannel (document.getState(), 1);
    REQUIRE (pattern.isValid());
    REQUIRE (channel.isValid());

    auto& undo = document.getUndoManager();
    undo.beginNewTransaction ("Add steps");

    for (int step = 0; step < 16; step += 4)
        dew::ProjectEdits::addNote (pattern, 1, step, 1, (int) channel[dew::ids::basePitch],
                                    1.0f, &undo);

    // The editor coalesces snapshot rebuilds through an AsyncUpdater, so the
    // pending rebuild has to be applied before the engine sees the change.
    component.flushPendingEngineUpdate();

    engine.rewind();
    REQUIRE (renderPeak() > 0.01f);

    // And undo must take it away again.
    REQUIRE (undo.undo());
    component.flushPendingEngineUpdate();

    engine.rewind();
    REQUIRE (juce::exactlyEqual (renderPeak(), 0.0f));
}

TEST_CASE ("opening the audio device either works or reports why", "[ui][audio][device]")
{
    // CI has no sound card. The requirement is that it fails with a message
    // rather than hanging or crashing, and that a device which does open is
    // describable - the status line shows exactly this string.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    dew::AudioEngine engine;
    dew::LiveAudioHost host (engine);

    const auto error = host.start();

    if (error.isEmpty())
    {
        INFO ("device: " << host.describeDevice());
        REQUIRE (host.describeDevice().isNotEmpty());
        REQUIRE (host.describeDevice() != "no audio device");
    }
    else
    {
        WARN ("no audio device available: " << error);
    }

    host.stop();
}

TEST_CASE ("every tab is painted, not only the active one", "[ui][smoke]")
{
    // The first version of dew's drawTabAreaBehindFrontButton filled its area
    // opaquely. JUCE hosts that in a component spanning the whole bar which sits
    // above every tab except the front one, so three of the four tabs vanished.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    dew::MainComponent component (false);
    component.setSize (1400, 700);

    juce::Component* bar = nullptr;

    std::function<void (juce::Component&)> findBar = [&] (juce::Component& parent)
    {
        for (auto* child : parent.getChildren())
        {
            if (auto* tabbed = dynamic_cast<juce::TabbedComponent*> (child); tabbed != nullptr && bar == nullptr)
                bar = &tabbed->getTabbedButtonBar();
            else
                findBar (*child);
        }
    };

    findBar (component);
    REQUIRE (bar != nullptr);
    REQUIRE (bar->getWidth() > 0);

    // Paint the whole editor, then look only at the tab strip.
    const auto image = renderToImage (component);
    const auto strip = bar->getLocalArea (&component, bar->getLocalBounds())
                          .withPosition (bar->getScreenPosition() - component.getScreenPosition());

    // Ink in each quarter of the occupied tab run: four tabs, four labels.
    auto* tabBar = dynamic_cast<juce::TabbedButtonBar*> (bar);
    REQUIRE (tabBar != nullptr);
    REQUIRE (tabBar->getNumTabs() == 4);

    for (int i = 0; i < tabBar->getNumTabs(); ++i)
    {
        auto* button = tabBar->getTabButton (i);
        REQUIRE (button != nullptr);
        REQUIRE (button->getWidth() > 20);

        const auto area = juce::Rectangle<int> (button->getX(), strip.getY(),
                                                button->getWidth(), button->getHeight())
                              .getIntersection (image.getBounds());
        REQUIRE (! area.isEmpty());

        // The label has to be legible: some pixels clearly lighter than the bar.
        int lightPixels = 0;

        for (int y = area.getY(); y < area.getBottom(); ++y)
            for (int x = area.getX(); x < area.getRight(); ++x)
                if (image.getPixelAt (x, y).getBrightness() > 0.35f)
                    ++lightPixels;

        INFO ("tab " << i << " (" << tabBar->getTabNames()[i] << ") light pixels: " << lightPixels);
        REQUIRE (lightPixels > 40);
    }
}

TEST_CASE ("a span selected in the editor loops the editor's own engine", "[ui][smoke][loop]")
{
    // The whole chain a person exercises by shift-dragging the ruler: the
    // editor state holds the span, MainComponent converts it and hands it to the
    // engine, and the engine confines the playhead. Each half is tested on its
    // own; this is the wiring between them.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    dew::MainComponent component;
    component.getDocument().setState (dew::ProjectFactory::createDemo(), true);
    component.documentWasReplaced();

    auto& engine = component.getEngine();
    engine.prepare (44100.0, 512);
    engine.setMode (dew::Transport::Mode::song);
    engine.rewind();
    engine.play();

    // Bars two to four of a four-bar demo, sixteen steps to the bar.
    component.getEditorState().setSelectedBarRange ({ 1, 3 });
    component.getEditorState().dispatchPendingMessages();

    REQUIRE (engine.hasLoopRegion (dew::Transport::Mode::song));
    REQUIRE (engine.getLoopRegion (dew::Transport::Mode::song).startSteps == 16.0f);
    REQUIRE (engine.getLoopRegion (dew::Transport::Mode::song).endSteps == 48.0f);

    juce::AudioBuffer<float> block (2, 512);

    const auto render = [&] { block.clear(); engine.processBlock (block); };

    // Into the region - a playhead before a loop plays into it rather than being
    // snapped, so this is not instant.
    for (int i = 0; i < 3000 && engine.getPlayheadSteps() < 16.0; ++i)
        render();

    REQUIRE (engine.getPlayheadSteps() >= 16.0);

    for (int i = 0; i < 600; ++i)
    {
        render();
        INFO ("playhead " << engine.getPlayheadSteps());
        REQUIRE (engine.getPlayheadSteps() >= 16.0);
        REQUIRE (engine.getPlayheadSteps() < 48.0);
    }

    // And clearing it hands the arrangement back.
    component.getEditorState().clearBarSelection();
    component.getEditorState().dispatchPendingMessages();

    REQUIRE_FALSE (engine.hasLoopRegion (dew::Transport::Mode::song));

    bool escaped = false;

    for (int i = 0; i < 900 && ! escaped; ++i)
    {
        render();
        escaped = engine.getPlayheadSteps() >= 48.0;
    }

    REQUIRE (escaped);
}
