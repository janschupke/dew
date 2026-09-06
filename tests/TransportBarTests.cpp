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
#include "ui/design/Icons.h"
#include "ui/primitives/DewControls.h"

using namespace dew;

namespace
{

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
