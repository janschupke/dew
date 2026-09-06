// The strip along the top: what it says about the transport, and when.
//
// Its own file rather than more of TransportTests.cpp, which is dew_engine's
// and links no UI at all.

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "app/ProjectDocument.h"
#include "model/ProjectFactory.h"
#include "ui/EditorState.h"
#include "ui/TransportBar.h"
#include "ui/design/DewLookAndFeel.h"
#include "i18n/Strings.h"
#include "ui/design/Icons.h"
#include "ui/primitives/DewControls.h"
#include "ui/MenuSeam.h"
#include "app/Settings.h"
#include "TestSupport.h"

using namespace dew;

namespace
{

/** A row's id and its tick, which menuItems() deliberately does not carry -
    it answers "what does this menu say", and these two tests ask "what does
    choosing this row do". Bound to a NAMED menu by taking a const&, for the
    reason MenuSeam.h gives: MenuItemIterator keeps a reference. */
int idOfRow (const juce::PopupMenu& menu, const juce::String& text)
{
    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        if (it.getItem().text == text)
            return it.getItem().itemID;

    return 0;
}

bool rowIsTicked (const juce::PopupMenu& menu, const juce::String& text)
{
    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        if (it.getItem().text == text)
            return it.getItem().isTicked;

    return false;
}

struct BarHarness
{
    BarHarness()
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        document.setState (ProjectFactory::createDefault(), true);
        bar.setSize (1400, tokens::size::stripTransport);
        bar.resized();
    }

    ~BarHarness()
    {
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    /** The play button, which is the first icon button on the strip. */
    DewIconButton& play()
    {
        for (auto* child : bar.getChildren())
            if (auto* button = dynamic_cast<DewIconButton*> (child))
                return *button;

        FAIL ("no icon button on the transport bar");
        return *dynamic_cast<DewIconButton*> (bar.getChildren().getFirst());
    }

    /** The mode button - the only DewButton on the strip. */
    DewButton& mode()
    {
        for (auto* child : bar.getChildren())
            if (auto* button = dynamic_cast<DewButton*> (child))
                return *button;

        FAIL ("no mode button on the transport bar");
        return *dynamic_cast<DewButton*> (bar.getChildren().getFirst());
    }

    /** An icon button by the tooltip it carries, which is the one thing that
        tells the four of them apart without depending on their order. */
    DewIconButton* buttonWithTooltip (const juce::String& tooltip)
    {
        for (auto* child : bar.getChildren())
            if (auto* button = dynamic_cast<DewIconButton*> (child))
                if (button->getTooltip() == tooltip)
                    return button;

        return nullptr;
    }

    DewLookAndFeel lookAndFeel;
    ProjectDocument document;
    AudioEngine engine;
    EditorState editorState;
    TransportBar bar { document, engine, editorState };
};

} // namespace

TEST_CASE ("the play button follows the engine, whatever moved it", "[transport][ui]")
{
    /*  The icon used to be written by the three things that could change it
        from the bar itself - its own click, the stop button's click, and
        refresh() - and AudioEngine is not a ChangeBroadcaster, so nothing told
        the bar when the transport moved any other way. Space, the Transport
        menu and an MCP client all left a playing transport showing a play
        triangle until something unrelated happened to call refresh().

        Driven through the engine directly, which is exactly the route that was
        broken: a test that clicked the button would have passed the whole time.
    */
    const juce::ScopedJuceInitialiser_GUI juceInit;
    BarHarness h;

    REQUIRE (h.play().getIcon() == icons::play());

    h.engine.play();
    h.bar.refreshPlayIcon();

    CHECK (h.play().getIcon() == icons::pause());

    h.engine.stop();
    h.bar.refreshPlayIcon();

    CHECK (h.play().getIcon() == icons::play());
}

TEST_CASE ("a poll that changes nothing changes nothing", "[transport][ui]")
{
    // The latch. This runs at motion::uiRefreshHz and setIcon repaints, so a
    // poll that wrote the icon every tick would repaint the strip sixty times a
    // second for ever.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    BarHarness h;

    h.engine.play();
    h.bar.refreshPlayIcon();

    const auto after = h.play().getIcon();

    for (int i = 0; i < 10; ++i)
        h.bar.refreshPlayIcon();

    CHECK (h.play().getIcon() == after);
}

TEST_CASE ("the mode button follows the engine, whatever moved it", "[transport][ui]")
{
    /*  The same defect the play icon had, in the one engine-owned control that
        was never joined to the poll that fixed it.

        refresh() set the mode button, and refresh() runs when the document is
        REPLACED - so cmd-L and the Transport menu both flipped the engine and
        left a stale fill AND a stale caption behind until something unrelated
        happened to open a project. What hid it is that the click handler wrote
        both halves itself, so the only route anybody drove was the one route
        that also updated the button.

        Driven through the engine directly, which is exactly the route cmd-L
        takes: DewApplication calls engine.setMode and nothing else.
    */
    const juce::ScopedJuceInitialiser_GUI juceInit;
    BarHarness h;

    h.engine.setMode (Transport::Mode::pattern);
    h.bar.refreshEngineState();

    const auto patternText = h.mode().getButtonText();
    CHECK_FALSE (h.mode().getToggleState());

    h.engine.setMode (Transport::Mode::song);
    h.bar.refreshEngineState();

    CHECK (h.mode().getToggleState());

    // The CAPTION as well as the fill. Both were written by the click handler,
    // so both went stale together, and a test that checked only the toggle
    // would have passed on a button still reading "Pattern".
    CHECK (h.mode().getButtonText() != patternText);

    h.engine.setMode (Transport::Mode::pattern);
    h.bar.refreshEngineState();

    CHECK_FALSE (h.mode().getToggleState());
    CHECK (h.mode().getButtonText() == patternText);
}

TEST_CASE ("the transport bar has a panic, and it is not where stop is", "[transport][ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    // A safety control, so two things about it are asserted rather than left to
    // reading: that it reaches the engine at all, and that it is not sitting
    // where a hand goes for stop.
    BarHarness h;

    auto* panic = h.buttonWithTooltip (tr (StringId::transport_panic_help));
    REQUIRE (panic != nullptr);

    auto* stop = h.buttonWithTooltip (tr (StringId::transport_stop_help));
    REQUIRE (stop != nullptr);

    // Play is still the first icon button in the bar, which is what the two
    // tests above find it by.
    CHECK (&h.play() != panic);

    // Separated from the transport group rather than butted against it.
    CHECK (panic->getX() > stop->getRight() + tokens::space::sm);

    auto fired = 0;
    h.bar.onPanic = [&fired] { ++fired; };

    h.engine.play();
    REQUIRE (h.engine.isPlaying());

    // onClick directly: triggerClick posts an async message, which a headless
    // harness never pumps - the same call every other button test here makes.
    panic->onClick();

    // Both halves: the engine's, which the bar does itself, and the hook for
    // what it cannot reach.
    CHECK_FALSE (h.engine.isPlaying());
    CHECK (fired == 1);
}

TEST_CASE ("the metronome button follows the engine, whatever moved it", "[transport][ui]")
{
    // The same defect the play icon and the mode button both had. Driven
    // through the engine directly, which is the route the Transport menu takes:
    // DewApplication calls setMetronomeEnabled and nothing else.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    BarHarness h;

    auto* metronome = h.buttonWithTooltip (tr (StringId::transport_metronome_help));
    REQUIRE (metronome != nullptr);
    CHECK_FALSE (metronome->getToggleState());

    h.engine.setMetronomeEnabled (true);
    h.bar.refreshEngineState();
    CHECK (metronome->getToggleState());

    h.engine.setMetronomeEnabled (false);
    h.bar.refreshEngineState();
    CHECK_FALSE (metronome->getToggleState());

    // And the button reaches the engine, so the one source of truth is written
    // from both directions.
    metronome->setToggleState (true, juce::dontSendNotification);
    metronome->onClick();
    CHECK (h.engine.isMetronomeEnabled());
}

TEST_CASE ("the keyboard button follows the mode the window owns", "[transport][ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    BarHarness h;

    auto* keyboard = h.buttonWithTooltip (tr (StringId::transport_keyboardInput_help));
    REQUIRE (keyboard != nullptr);

    auto typing = false;
    h.bar.isKeyboardInputEnabled = [&typing] { return typing; };

    auto toggled = 0;
    h.bar.onToggleKeyboardInput = [&toggled] { ++toggled; };

    typing = true;
    h.bar.refreshEngineState();
    CHECK (keyboard->getToggleState());

    typing = false;
    h.bar.refreshEngineState();
    CHECK_FALSE (keyboard->getToggleState());

    keyboard->onClick();
    CHECK (toggled == 1);
}

TEST_CASE ("the metronome's right-click offers the count-in", "[transport][ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    BarHarness h;

    CHECK_FALSE (h.bar.isCountInEnabled());

    // Bound to a NAMED local: PopupMenu::MenuItemIterator keeps a reference,
    // and iterating a temporary walks a menu that has already gone.
    const auto menu = h.bar.buildMetronomeMenu();
    const auto label = tr (StringId::transport_countIn_label);

    REQUIRE (menuItems (menu) == juce::StringArray { label });
    CHECK_FALSE (rowIsTicked (menu, label));

    const auto countIn = idOfRow (menu, label);
    REQUIRE (countIn != 0);

    h.bar.applyMetronomeChoice (countIn);
    CHECK (h.bar.isCountInEnabled());

    const auto after = h.bar.buildMetronomeMenu();
    CHECK (rowIsTicked (after, label));

    h.bar.applyMetronomeChoice (countIn);
    CHECK_FALSE (h.bar.isCountInEnabled());

    // A choice nobody offered - the id a dismissed menu returns - must not
    // flip it.
    h.bar.applyMetronomeChoice (0);
    CHECK_FALSE (h.bar.isCountInEnabled());
}

TEST_CASE ("the click and the count-in survive a restart", "[transport][ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    BarHarness h;

    // A settings file of its own, so a test can never touch the user's real
    // one - the rule tests/SettingsTests.cpp already writes down.
    testing::TempDir temp { "dew-transport-settings-" };
    Settings settings { temp.dir };

    h.engine.setMetronomeEnabled (true);

    const auto menu = h.bar.buildMetronomeMenu();
    h.bar.applyMetronomeChoice (idOfRow (menu, tr (StringId::transport_countIn_label)));
    REQUIRE (h.bar.isCountInEnabled());

    h.bar.captureMetronomeSettings (settings);

    // A second bar over a second engine, the way a relaunch is.
    AudioEngine restarted;
    EditorState state;
    TransportBar restoredBar { h.document, restarted, state };

    restoredBar.applyMetronomeSettings (settings);

    CHECK (restarted.isMetronomeEnabled());
    CHECK (restoredBar.isCountInEnabled());

    // Right in the FIRST frame rather than after the first poll: a restored
    // session that flickers is one somebody notices.
    for (auto* child : restoredBar.getChildren())
        if (auto* button = dynamic_cast<DewIconButton*> (child))
            if (button->getTooltip() == tr (StringId::transport_metronome_help))
                CHECK (button->getToggleState());
}
