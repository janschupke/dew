#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "io/LiveAudioHost.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "app/Settings.h"
#include "ui/MainComponent.h"
#include "PaintProbe.h"
#include "PlaylistHarness.h"
#include "FixtureProject.h"

using namespace dew::testing;

namespace
{

/** Paints a component into an offscreen image.

    This is how the UI is verified in CI and on machines where screen-recording
    permission is not granted: no display, no window server, just the paint
    path. A component that silently failed to lay out paints a flat fill, which
    these tests can tell apart from one that drew something.
*/
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

    dew::MainComponent component (false);

    REQUIRE (component.getWidth() > 0);
    REQUIRE (component.getHeight() > 0);

    const auto image = render (component);
    REQUIRE (image.isValid());

    const auto content = fractionOfNonBackgroundPixels (image);
    INFO ("non-background pixel fraction: " << content);
    REQUIRE (content > 0.01f);
}

TEST_CASE ("the instrument panel folds away, and comes back", "[ui][smoke]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    dew::MainComponent component (false);
    component.setSize (1400, 900);

    // The VIEWPORT is what folds. The panel is the scrolled component inside
    // it, so it keeps a width of its own while hidden - which is the whole
    // point: the viewport is what stops the panel clipping when the window is
    // too short to hold it.
    auto* panel = component.findChildWithID ("instrumentPanelViewport");
    auto* divider = component.findChildWithID ("panelDivider");

    REQUIRE (panel != nullptr);
    REQUIRE (divider != nullptr);

    const auto openWidth = panel->getWidth();
    REQUIRE (openWidth > 0);

    // In the TAB STRIP, which reserves width for it, rather than on the divider,
    // which straddles the seam and floated it over the last tab and over the
    // panel's title band. findChildWithID is not recursive and the strip is two
    // levels down, so this walks.
    auto* toggle = dynamic_cast<juce::Button*> (findDescendantWithID (component, "panelToggle"));
    REQUIRE (toggle != nullptr);

    toggle->onClick();

    // Hidden as well as given no width: a zero-width panel still lays its
    // children out and paints, and its knobs would keep taking the clicks meant
    // for the editor beside it.
    CHECK (panel->getWidth() == 0);
    CHECK_FALSE (panel->isVisible());

    // The toggle lives in the tab strip, not in the panel, so it is still there
    // to press once the panel it hides is gone.
    CHECK (toggle->isVisible());
    CHECK (toggle->getWidth() > 0);
    CHECK (divider->getWidth() > 0);
    CHECK (divider->isVisible());

    // And it is INSIDE the strip rather than over it, which is the whole
    // change: the tab bar gave up the width instead of being painted on.
    auto* tabs = dynamic_cast<juce::TabbedComponent*> (component.findChildWithID ("editorTabs"));
    REQUIRE (tabs != nullptr);

    const auto strip = tabs->getLocalArea (nullptr, tabs->getTabbedButtonBar().getScreenBounds());
    const auto chevron = tabs->getLocalArea (nullptr, toggle->getScreenBounds());

    CHECK (chevron.getY() >= strip.getY());
    CHECK (chevron.getBottom() <= strip.getBottom());
    CHECK (chevron.getX() >= strip.getRight());

    toggle->onClick();

    CHECK (panel->getWidth() == openWidth);
    CHECK (panel->isVisible());
}

TEST_CASE ("the editor survives being resized to its limits", "[ui][smoke]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    dew::MainComponent component (false);

    for (auto size : { juce::Point<int> { 900, 560 }, juce::Point<int> { 2400, 1400 } })
    {
        component.setSize (size.x, size.y);
        const auto image = render (component);

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

    dew::MainComponent component (false);
    component.getDocument().setState (dew::testing::fixtureProject(), true);
    component.documentWasReplaced();
    component.setSize (1280, 800);

    juce::TabbedComponent* tabs = nullptr;

    for (auto* child : component.getChildren())
        if (auto* asTabs = dynamic_cast<juce::TabbedComponent*> (child))
            tabs = asTabs;

    REQUIRE (tabs != nullptr);
    REQUIRE (tabs->getNumTabs() == dew::Settings::numTabs);

    for (int i = 0; i < tabs->getNumTabs(); ++i)
    {
        tabs->setCurrentTabIndex (i, true);
        component.resized();

        const auto image = render (component);

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

    dew::MainComponent component (false);
    component.getDocument().setState (dew::testing::fixtureProject(), true);
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

    dew::MainComponent component (false);
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
        dew::ProjectEdits::addNote (pattern, 1, step, 1, (int) channel[dew::ids::basePitch], 1.0f,
                                    &undo);

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
            if (auto* tabbed = dynamic_cast<juce::TabbedComponent*> (child);
                tabbed != nullptr && bar == nullptr)
                bar = &tabbed->getTabbedButtonBar();
            else
                findBar (*child);
        }
    };

    findBar (component);
    REQUIRE (bar != nullptr);
    REQUIRE (bar->getWidth() > 0);

    // Paint the whole editor, then look only at the tab strip.
    const auto image = render (component);
    const auto strip = bar->getLocalArea (&component, bar->getLocalBounds())
                           .withPosition (bar->getScreenPosition() - component.getScreenPosition());

    // Ink under each tab: one label apiece.
    auto* tabBar = dynamic_cast<juce::TabbedButtonBar*> (bar);
    REQUIRE (tabBar != nullptr);
    REQUIRE (tabBar->getNumTabs() == dew::Settings::numTabs);

    for (int i = 0; i < tabBar->getNumTabs(); ++i)
    {
        auto* button = tabBar->getTabButton (i);
        REQUIRE (button != nullptr);
        REQUIRE (button->getWidth() > 20);

        const auto area = juce::Rectangle<int> (button->getX(), strip.getY(), button->getWidth(),
                                                button->getHeight())
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

    dew::MainComponent component (false);
    component.getDocument().setState (dew::testing::fixtureProject(), true);
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
    REQUIRE (
        juce::exactlyEqual (engine.getLoopRegion (dew::Transport::Mode::song).startSteps, 16.0f));
    REQUIRE (
        juce::exactlyEqual (engine.getLoopRegion (dew::Transport::Mode::song).endSteps, 48.0f));

    juce::AudioBuffer<float> block (2, 512);

    const auto render = [&]
    {
        block.clear();
        engine.processBlock (block);
    };

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

TEST_CASE ("a channel is selected whatever the project's first one is called", "[ui][smoke]")
{
    // The pattern id has been answered against the document since it was
    // written - TransportBar::setCurrentPattern falls back to the first PATTERN
    // there is - and the channel id was not, so a remembered id was a claim
    // about a project the settings file has never seen and nothing checked it.
    //
    // It looked right only because ProjectFactory::createDefault always makes a
    // channel with id 1. Open a project whose channel 1 was deleted and nothing
    // is selected at all: no rack row highlighted, the roll showing its empty
    // state, the instrument panel disabled, and live MIDI playing nothing.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    dew::MainComponent component (false);
    component.setSize (1400, 900);

    auto project = dew::ProjectFactory::createDefault();
    juce::UndoManager setup;

    auto first = dew::ProjectEdits::findChannel (project, 1);
    REQUIRE (first.isValid());
    dew::ProjectEdits::removeChannel (project, first, &setup);

    REQUIRE_FALSE (dew::ProjectEdits::findChannel (project, 1).isValid());

    // setState then documentWasReplaced, which is what New and Open do (see
    // DewApplication) and what dew_shot --project does. setState alone reaches
    // the shell through AsyncUpdater, which a headless test never delivers.
    component.getDocument().setState (project, true);
    component.documentWasReplaced();
    component.setSize (1400, 900);

    const auto selected = component.getEditorState().getSelectedChannelId();

    INFO ("selected channel " << selected);
    CHECK (selected != 1);
    CHECK (dew::ProjectEdits::findChannel (component.getDocument().getState(), selected).isValid());

    // And it is resolved BEFORE the panels are refreshed, not after. Every one
    // of them reads the selection: a panel refreshed against a channel that is
    // not there disables itself, and the later resolve in projectChanged moves
    // the id without refreshing it again. EditorState is a ChangeBroadcaster,
    // so in the running application the listener eventually catches up - which
    // is exactly the kind of "works if the loop turns" this ordering removes.
    auto* panel = findDescendantWithID (component, "instrumentPanel");
    REQUIRE (panel != nullptr);
    CHECK (panel->isEnabled());

    // Deleting the SELECTED channel runs the same resolver, from
    // projectChanged. Not asserted here: that path arrives through
    // AsyncUpdater, which posts on the platform event loop and is never
    // delivered in a headless test - a juce::Timer fires anyway, so the loop
    // looks alive while every posted callback sits there. A test that called
    // the resolver directly would prove the resolver, which the case above
    // already does.
}
