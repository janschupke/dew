#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Settings.h"

using namespace dew;

namespace
{

/** A settings file in a temporary directory, removed afterwards, so a test can
    never touch the user's real one.
*/
struct TempSettings
{
    TempSettings()
        : directory (juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("dew-settings-test-" + juce::Uuid().toDashedString()))
    {
        directory.createDirectory();
    }

    ~TempSettings() { directory.deleteRecursively(); }

    std::unique_ptr<Settings> open() { return std::make_unique<Settings> (directory); }

    juce::File directory;
};

} // namespace

TEST_CASE ("a collapsed instrument panel survives a relaunch", "[settings]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    TempSettings temp;

    {
        auto settings = temp.open();

        // Kept beside the width rather than derived from it: a folded panel has
        // to come back to the width it had.
        CHECK_FALSE (settings->getPanelCollapsed());
        settings->setPanelWidth (420);
        settings->setPanelCollapsed (true);
        settings->flush();
    }

    auto settings = temp.open();
    CHECK (settings->getPanelCollapsed());
    CHECK (settings->getPanelWidth() == 420);
}

TEST_CASE ("settings round-trip to disk", "[settings]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    TempSettings temp;

    {
        auto settings = temp.open();
        settings->setTabIndex (2);
        settings->setSelectedChannelId (7);
        settings->setSelectedMixerTrackId (3);
        settings->setCurrentPatternId (4);
        settings->setPianoRollZoom (48.0);
        settings->setPianoRollScroll (12.5);
        settings->setPianoRollPitchScroll (300.0);
        settings->setPanelWidth (360);
        settings->flush();
    }

    auto reopened = temp.open();

    REQUIRE (reopened->getTabIndex() == 2);
    REQUIRE (reopened->getSelectedChannelId() == 7);
    REQUIRE (reopened->getSelectedMixerTrackId() == 3);
    REQUIRE (reopened->getCurrentPatternId() == 4);
    REQUIRE (juce::exactlyEqual (reopened->getPianoRollZoom(), 48.0));
    REQUIRE (juce::exactlyEqual (reopened->getPianoRollScroll(), 12.5));
    REQUIRE (juce::exactlyEqual (reopened->getPianoRollPitchScroll(), 300.0));
    REQUIRE (reopened->getPanelWidth() == 360);
}

TEST_CASE ("a fresh install gets sensible defaults", "[settings]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    TempSettings temp;

    auto settings = temp.open();

    REQUIRE (settings->getTabIndex() == 0);
    REQUIRE (settings->getSelectedChannelId() == 1);
    REQUIRE (settings->getCurrentPatternId() == 1);
    REQUIRE (settings->getPanelWidth() == Settings::defaultPanelWidth);
    REQUIRE (settings->getWindowState().isEmpty());
    REQUIRE (settings->getAudioState() == nullptr);
}

TEST_CASE ("out-of-range values fall back rather than being restored", "[settings]")
{
    // A settings file can be edited, corrupted, or written by a build that had
    // different limits. Restoring a state the user cannot recover from is worse
    // than losing the preference.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    TempSettings temp;

    auto settings = temp.open();

    settings->setTabIndex (99);
    REQUIRE (settings->getTabIndex() < Settings::numTabs);

    settings->setTabIndex (-4);
    REQUIRE (settings->getTabIndex() >= 0);

    settings->setPanelWidth (5000);
    REQUIRE (settings->getPanelWidth() <= Settings::maxPanelWidth);

    settings->setPanelWidth (10);
    REQUIRE (settings->getPanelWidth() >= Settings::minPanelWidth);

    settings->setPianoRollZoom (1.0e9);
    REQUIRE (settings->getPianoRollZoom() <= 120.0);

    settings->setPianoRollScroll (-500.0);
    REQUIRE (settings->getPianoRollScroll() >= 0.0);
}

TEST_CASE ("a window rectangle that is entirely offscreen is refused", "[settings]")
{
    // Restoring one cannot be undone by dragging, because there is nothing on
    // screen to drag.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    REQUIRE (! Settings::isWindowStateUsable (""));
    REQUIRE (! Settings::isWindowStateUsable ("garbage"));
    REQUIRE (! Settings::isWindowStateUsable ("100 100"));           // too few numbers
    REQUIRE (! Settings::isWindowStateUsable ("100 100 10 10"));     // absurdly small
    REQUIRE (! Settings::isWindowStateUsable ("-9000 -9000 1200 800"));

    // Somewhere on a real display, which is where the app actually opens.
    const auto display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();

    if (display == nullptr)
    {
        WARN ("no displays on this machine; skipping the on-screen case");
        return;
    }

    const auto area = display->userBounds;
    const auto onScreen = juce::String (area.getX() + 20) + " " + juce::String (area.getY() + 20)
                        + " 1000 700";

    REQUIRE (Settings::isWindowStateUsable (onScreen));

    // JUCE prefixes a maximised state; that has to survive the check.
    REQUIRE (Settings::isWindowStateUsable ("fs " + onScreen));
}

TEST_CASE ("the audio device state survives a restart", "[settings]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    TempSettings temp;

    {
        auto settings = temp.open();

        juce::XmlElement state ("DEVICESETUP");
        state.setAttribute ("audioOutputDeviceName", "Some Interface");
        state.setAttribute ("audioDeviceRate", 48000);

        settings->setAudioState (&state);
        settings->flush();
    }

    auto reopened = temp.open();
    const auto restored = reopened->getAudioState();

    REQUIRE (restored != nullptr);
    REQUIRE (restored->getStringAttribute ("audioOutputDeviceName") == "Some Interface");
    REQUIRE (restored->getIntAttribute ("audioDeviceRate") == 48000);

    // And clearing it really clears it.
    reopened->setAudioState (nullptr);
    REQUIRE (reopened->getAudioState() == nullptr);
}

// --- applied to the editor ---------------------------------------------------

#include "ui/MainComponent.h"

TEST_CASE ("a session is restored into the editor and captured back out", "[settings][ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    TempSettings temp;

    {
        auto settings = temp.open();
        settings->setTabIndex (2);
        settings->setSelectedChannelId (3);
        settings->setSelectedMixerTrackId (2);
        settings->setCurrentPatternId (1);
        settings->setPianoRollZoom (36.0);
        settings->setPianoRollScroll (8.0);
        settings->setPianoRollPitchScroll (420.0);
        settings->setPanelWidth (380);

        dew::MainComponent component (false);
        component.setSize (1400, 800);
        component.applySettings (*settings);

        // The editor is showing what was saved.
        REQUIRE (component.getEditorState().getSelectedChannelId() == 3);
        REQUIRE (component.getEditorState().getSelectedMixerTrackId() == 2);

        // And capturing it back out returns the same values.
        auto roundTripped = temp.open();
        component.captureSettings (*roundTripped);

        REQUIRE (roundTripped->getTabIndex() == 2);
        REQUIRE (roundTripped->getSelectedChannelId() == 3);
        REQUIRE (roundTripped->getSelectedMixerTrackId() == 2);
        REQUIRE (roundTripped->getPanelWidth() == 380);
        REQUIRE (juce::exactlyEqual (roundTripped->getPianoRollZoom(), 36.0));
        REQUIRE (juce::exactlyEqual (roundTripped->getPianoRollPitchScroll(), 420.0));

        // Scroll is clamped to what the project can actually show: a new
        // document's 16-step pattern fits in the view, so there is nowhere to
        // scroll to and restoring an offset into it would be wrong.
        INFO ("restored scroll " << roundTripped->getPianoRollScroll());
        REQUIRE (roundTripped->getPianoRollScroll() >= 0.0);
        REQUIRE (roundTripped->getPianoRollScroll() <= 8.0);
    }
}

TEST_CASE ("the instrument panel can be resized, and the width is what is saved", "[settings][ui]")
{
    // It was a hard-coded 300px, so there was no position to remember.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    TempSettings temp;

    dew::MainComponent component (false);
    component.setSize (1400, 800);

    auto settings = temp.open();
    settings->setPanelWidth (420);
    component.applySettings (*settings);

    // The panel really is that wide, not merely recorded as being.
    juce::Component* panel = nullptr;

    std::function<void (juce::Component&)> find = [&] (juce::Component& parent)
    {
        for (auto* child : parent.getChildren())
        {
            if (child->getComponentID() == "instrumentPanel")
                panel = child;
            else
                find (*child);
        }
    };

    find (component);

    REQUIRE (panel != nullptr);
    INFO ("panel width " << panel->getWidth());
    REQUIRE (panel->getWidth() == 420);
}

// --- rendering ---------------------------------------------------------------

TEST_CASE ("render settings round-trip to disk", "[settings][render]")
{
    TempSettings temp;

    {
        auto settings = temp.open();

        settings->setRenderFormat (2);
        settings->setRenderSampleRate (48000);
        settings->setRenderBitDepth (16);
        settings->setRenderTailSeconds (2.5);
        settings->setRenderNormalize (true);
        settings->flush();
    }

    auto reopened = temp.open();

    REQUIRE (reopened->getRenderFormat() == 2);
    REQUIRE (reopened->getRenderSampleRate() == 48000);
    REQUIRE (reopened->getRenderBitDepth() == 16);
    REQUIRE (std::abs (reopened->getRenderTailSeconds() - 2.5) < 1.0e-9);
    REQUIRE (reopened->getRenderNormalize());
}

TEST_CASE ("nonsense render settings fall back rather than being restored",
           "[settings][render]")
{
    TempSettings temp;

    {
        auto settings = temp.open();

        settings->setRenderFormat (99);          // clamped on the way in
        settings->setRenderSampleRate (12345);   // not a rate anything offers
        settings->setRenderBitDepth (7);         // not a depth anything writes
        settings->setRenderTailSeconds (1.0e9);
        settings->flush();
    }

    auto reopened = temp.open();

    REQUIRE (reopened->getRenderFormat() <= 3);
    REQUIRE (reopened->getRenderSampleRate() == 44100);
    REQUIRE (reopened->getRenderBitDepth() == 24);
    REQUIRE (reopened->getRenderTailSeconds() <= 30.0);
}

TEST_CASE ("a render directory that has gone away is not restored", "[settings][render]")
{
    TempSettings temp;

    const auto vanished = temp.directory.getChildFile ("gone");
    vanished.createDirectory();

    {
        auto settings = temp.open();
        settings->setLastRenderDirectory (vanished);
        settings->flush();
    }

    vanished.deleteRecursively();

    auto reopened = temp.open();

    // Opening a chooser at a path that no longer exists - an unplugged drive,
    // a deleted folder - is worse than opening it somewhere ordinary.
    REQUIRE (reopened->getLastRenderDirectory() != vanished);
    REQUIRE (reopened->getLastRenderDirectory().isDirectory());
}

TEST_CASE ("a render directory that still exists comes back", "[settings][render]")
{
    TempSettings temp;

    const auto kept = temp.directory.getChildFile ("renders");
    kept.createDirectory();

    {
        auto settings = temp.open();
        settings->setLastRenderDirectory (kept);
        settings->flush();
    }

    auto reopened = temp.open();

    REQUIRE (reopened->getLastRenderDirectory() == kept);
}
