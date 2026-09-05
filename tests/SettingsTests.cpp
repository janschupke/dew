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

    ~TempSettings()
    {
        directory.deleteRecursively();
    }

    std::unique_ptr<Settings> open()
    {
        return std::make_unique<Settings> (directory);
    }

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

    // 0 means "never set", which the playlist reads as "keep your default" -
    // so an install that predates the control opens at the height it always had.
    REQUIRE (settings->getPlaylistTrackHeight() == 0);
    REQUIRE (settings->getMixerEffectBandRows() == 0);
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
    REQUIRE (! Settings::isWindowStateUsable ("100 100"));       // too few numbers
    REQUIRE (! Settings::isWindowStateUsable ("100 100 10 10")); // absurdly small
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

#include "model/Ids.h"
#include "ui/MainComponent.h"
#include "ui/design/Tokens.h"

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
        settings->setPlaylistTrackHeight (96);
        settings->setMixerEffectBandRows (tokens::size::effectBandRowsMax);

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

        // The lane height makes the whole trip: Settings -> EditorTabs ->
        // PlaylistComponent and back. Asserting it here rather than on a
        // PropertiesFile write is what covers the CLAMP as well, which lives in
        // the playlist because dew_app cannot see the size ladder.
        REQUIRE (roundTripped->getPlaylistTrackHeight() == 96);

        // The effect band makes the same trip, through the mixer, and is
        // clamped in the same place and for the same reason.
        REQUIRE (roundTripped->getMixerEffectBandRows() == tokens::size::effectBandRowsMax);
    }

    // And an absurd band comes back inside the range, which is the other half
    // of "stored raw, clamped by the view".
    {
        auto settings = temp.open();
        settings->setMixerEffectBandRows (1000);

        dew::MainComponent component (false);
        component.setSize (1400, 800);
        component.applySettings (*settings);

        auto roundTripped = temp.open();
        component.captureSettings (*roundTripped);

        REQUIRE (roundTripped->getMixerEffectBandRows() <= tokens::size::effectBandRowsMax);
        REQUIRE (roundTripped->getMixerEffectBandRows() >= tokens::size::effectBandRowsMin);
    }

    // Out of range on the way in comes back inside it, because the playlist
    // clamps what it is given. This is the leg that documents why Settings
    // stores the number raw: the bounds are the ladder's, and dew_app is a leaf
    // that cannot see the design library.
    {
        auto settings = temp.open();
        settings->setPlaylistTrackHeight (10000);

        dew::MainComponent component (false);
        component.setSize (1400, 800);
        component.applySettings (*settings);

        auto roundTripped = temp.open();
        component.captureSettings (*roundTripped);

        INFO ("restored height " << roundTripped->getPlaylistTrackHeight());
        REQUIRE (roundTripped->getPlaylistTrackHeight() <= dew::tokens::size::trackHeightMax);
        REQUIRE (roundTripped->getPlaylistTrackHeight() >= dew::tokens::size::trackHeightMin);
    }
}

TEST_CASE ("a remembered pattern that the project has not got opens the first one",
           "[settings][ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    TempSettings temp;

    // The startup path used to write the remembered id straight into
    // EditorState. Settings::getCurrentPatternId only clamps it to one or more,
    // and the project a settings file was written beside is deliberately NOT
    // remembered - so any id above the pattern count was restored intact and
    // pointed the whole editor at a pattern that does not exist: a blank
    // dropdown, a disabled length field, an empty roll, and a sequencer with no
    // material, which in pattern mode is silence.
    auto settings = temp.open();
    settings->setCurrentPatternId (4242);

    dew::MainComponent component (false);
    component.setSize (1400, 800);
    component.applySettings (*settings);

    const auto& project = component.getDocument().getState();
    const auto first = project.getChildWithName (dew::ids::PATTERN);
    REQUIRE (first.isValid());

    const auto firstId = (int) first[dew::ids::id];
    const auto restored = component.getEditorState().getCurrentPatternId();

    INFO ("restored pattern " << restored << ", first is " << firstId);
    CHECK (restored == firstId);

    // The ENGINE too. EditorState and the engine hold the same id separately,
    // and the path that only set the first one is how a restored session could
    // show one pattern and play another.
    CHECK (component.getEngine().getCurrentPatternId() == firstId);

    // Control case: an id the project HAS is left exactly where it is, so this
    // is a fallback rather than a reset.
    auto sane = temp.open();
    sane->setCurrentPatternId (firstId);

    dew::MainComponent second (false);
    second.setSize (1400, 800);
    second.applySettings (*sane);

    CHECK (second.getEditorState().getCurrentPatternId() == firstId);
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

TEST_CASE ("nonsense render settings fall back rather than being restored", "[settings][render]")
{
    TempSettings temp;

    {
        auto settings = temp.open();

        settings->setRenderFormat (99);        // clamped on the way in
        settings->setRenderSampleRate (12345); // not a rate anything offers
        settings->setRenderBitDepth (7);       // not a depth anything writes
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

TEST_CASE ("the interface scale survives a relaunch, and a broken one does not", "[settings]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    TempSettings temp;

    {
        auto settings = temp.open();

        // 1.0 is the size dew was drawn at, so a settings file that predates
        // the control has to read as "leave it alone".
        CHECK (juce::exactlyEqual (settings->getUiScale(), Settings::defaultUiScale));

        settings->setUiScale (1.5);
        settings->flush();
    }

    {
        auto settings = temp.open();
        CHECK (juce::exactlyEqual (settings->getUiScale(), 1.5));

        // Out of range in either direction is clamped rather than restored: a
        // scale of twelve is a window nobody can reach the menu bar of to
        // undo it.
        settings->setUiScale (12.0);
        CHECK (juce::exactlyEqual (settings->getUiScale(), Settings::maxUiScale));

        settings->setUiScale (0.1);
        CHECK (juce::exactlyEqual (settings->getUiScale(), Settings::minUiScale));
    }
}

TEST_CASE ("every offered interface scale is one the file will keep", "[settings]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    TempSettings temp;
    auto settings = temp.open();

    // The View menu offers these four. A step outside the clamp would be an
    // item that silently does something else when you pick it.
    for (int step = 0; step < Settings::numUiScaleSteps; ++step)
    {
        INFO ("step " << step);
        settings->setUiScale (Settings::uiScaleSteps[step]);
        CHECK (juce::exactlyEqual (settings->getUiScale(), Settings::uiScaleSteps[step]));
    }
}

TEST_CASE ("the piano roll's row height and the score's text size are remembered", "[settings]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    TempSettings temp;

    {
        auto settings = temp.open();

        // 0 is "never set", which the roll reads as "keep the default" - the
        // same contract the playlist's lane height already has.
        CHECK (settings->getPianoRollRowHeight() == 0);
        CHECK (settings->getScoreFontStep() == 0);

        settings->setPianoRollRowHeight (28);
        settings->setScoreFontStep (2);
        settings->flush();
    }

    {
        auto settings = temp.open();
        CHECK (settings->getPianoRollRowHeight() == 28);
        CHECK (settings->getScoreFontStep() == 2);

        // Negative is the one thing refused here. What the upper bound is
        // belongs to the ladder, which this layer cannot see.
        settings->setPianoRollRowHeight (-40);
        settings->setScoreFontStep (-1);
        CHECK (settings->getPianoRollRowHeight() == 0);
        CHECK (settings->getScoreFontStep() == 0);
    }
}
