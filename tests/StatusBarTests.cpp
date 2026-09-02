#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "io/LiveAudioHost.h"
#include "model/Ids.h"
#include "model/ProjectDocument.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "ui/EditorState.h"
#include "ui/StatusBar.h"

using namespace dew;

namespace
{

struct StatusHarness
{
    StatusHarness()
    {
        document.setState (ProjectFactory::createDemo(), true);
        bar.setSize (1200, StatusBar::barHeight);
        bar.setVisible (true);
        bar.refresh();
        bar.resized();
    }

    ProjectDocument document;
    AudioEngine engine;
    LiveAudioHost audioHost { engine };
    EditorState editorState;
    StatusBar bar { document, editorState, audioHost };
};

} // namespace

TEST_CASE ("the status bar says what the editors are pointed at", "[statusbar]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    StatusHarness h;

    const auto channel = h.document.getState().getChildWithName (ids::CHANNEL);
    const auto name = channel[ids::name].toString();

    INFO ("context: " << h.bar.getContextText());
    REQUIRE (h.bar.getContextText().contains (name));
    REQUIRE (h.bar.getContextText().contains ("steps"));
    REQUIRE (h.bar.getContextText().contains ("note"));

    // Selecting a different channel changes it.
    juce::Array<int> channelIds;

    for (const auto& c : h.document.getState())
        if (c.hasType (ids::CHANNEL))
            channelIds.add ((int) c[ids::id]);

    REQUIRE (channelIds.size() > 1);
    h.editorState.setSelectedChannelId (channelIds.getLast());

    // EditorState broadcasts asynchronously and a console test runs no message
    // loop, so drive the refresh the timer would have done.
    h.bar.refresh();

    INFO ("after selecting another channel: " << h.bar.getContextText());
    REQUIRE (! h.bar.getContextText().contains (name));
}

TEST_CASE ("a message expires instead of standing forever", "[statusbar]")
{
    // Status used to be a label three call sites overwrote, with no timeout, so
    // whichever wrote last stayed until something else happened to write.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    StatusHarness h;

    h.bar.showMessage ("Saved to demo.dew", StatusBar::Severity::success);

    REQUIRE (h.bar.hasMessage());
    REQUIRE (h.bar.getMessageText() == "Saved to demo.dew");

    h.bar.advanceMessageClock (StatusBar::messageLifetimeMs / 2);
    REQUIRE (h.bar.hasMessage());

    h.bar.advanceMessageClock (StatusBar::messageLifetimeMs);
    REQUIRE (! h.bar.hasMessage());
}

TEST_CASE ("a routine message cannot bury an error", "[statusbar]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    StatusHarness h;

    h.bar.showMessage ("Audio unavailable: no device", StatusBar::Severity::error);
    REQUIRE (h.bar.getMessageSeverity() == StatusBar::Severity::error);

    // Something routine arriving a moment later must not displace it.
    h.bar.showMessage ("Saved", StatusBar::Severity::success);
    REQUIRE (h.bar.getMessageText().contains ("Audio unavailable"));

    // But another error does.
    h.bar.showMessage ("Could not write the file", StatusBar::Severity::error);
    REQUIRE (h.bar.getMessageText() == "Could not write the file");

    // And once the error has expired, anything can take its place.
    h.bar.advanceMessageClock (StatusBar::messageLifetimeMs + 1);
    h.bar.showMessage ("Saved", StatusBar::Severity::success);
    REQUIRE (h.bar.getMessageText() == "Saved");
}

TEST_CASE ("the status bar paints its three regions", "[statusbar]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    StatusHarness h;

    h.bar.showMessage ("Something happened", StatusBar::Severity::warning);

    juce::Image image (juce::Image::ARGB, h.bar.getWidth(), h.bar.getHeight(), true);
    juce::Graphics g (image);
    h.bar.paintEntireComponent (g, true);

    const auto inkIn = [&image] (juce::Rectangle<int> area)
    {
        int lit = 0;

        for (int y = area.getY(); y < area.getBottom(); ++y)
            for (int x = area.getX(); x < area.getRight(); ++x)
                if (image.getPixelAt (x, y).getBrightness() > 0.4f)
                    ++lit;

        return lit;
    };

    const auto third = h.bar.getWidth() / 3;

    // Context on the left, the message in the middle, load and drops on the right.
    REQUIRE (inkIn ({ 0, 0, third, h.bar.getHeight() }) > 20);
    REQUIRE (inkIn ({ third, 0, third, h.bar.getHeight() }) > 20);
    REQUIRE (inkIn ({ third * 2, 0, third, h.bar.getHeight() }) > 20);
}

TEST_CASE ("the status bar follows a replaced document", "[statusbar]")
{
    // Every other panel is refreshed when the document is swapped; leaving the
    // status bar out made it describe the project that was open before.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    AudioEngine engine;
    LiveAudioHost audioHost { engine };
    EditorState editorState;

    document.setState (ProjectFactory::createDefault(), true);

    StatusBar bar { document, editorState, audioHost };
    bar.setSize (1200, StatusBar::barHeight);
    bar.setVisible (true);
    bar.refresh();

    REQUIRE (bar.getContextText().contains ("Pattern 1"));

    document.setState (ProjectFactory::createDemo(), true);
    bar.refresh();

    INFO ("context: " << bar.getContextText());
    REQUIRE (bar.getContextText().contains ("Groove"));
    REQUIRE (! bar.getContextText().contains ("0 notes"));
}
