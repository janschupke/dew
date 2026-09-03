#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "app/ProjectDocument.h"
#include "ui/EditorState.h"
#include "ui/RenderPanel.h"
#include "FixtureProject.h"

using namespace dew;
using Catch::Approx;

namespace
{

struct PanelHarness
{
    PanelHarness()
    {
        document.setState (dew::testing::fixtureProject(), true);
        panel.setSize (RenderPanel::preferredWidth, panel.getRequiredHeight());
        panel.setVisible (true);
        panel.resized();
    }

    void relayout()
    {
        panel.setSize (RenderPanel::preferredWidth, panel.getRequiredHeight());
        panel.resized();
    }

    ProjectDocument document;
    EditorState editorState;
    RenderPanel panel { document, editorState, nullptr };
};

float fractionOfNonBackgroundPixels (const juce::Image& image)
{
    int lit = 0, total = 0;

    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
        {
            ++total;

            if (image.getPixelAt (x, y) != image.getPixelAt (0, 0))
                ++lit;
        }

    return total > 0 ? (float) lit / (float) total : 0.0f;
}

} // namespace

TEST_CASE ("the render panel builds and paints headless", "[ui][render]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PanelHarness h;

    juce::Image image (juce::Image::ARGB, h.panel.getWidth(), h.panel.getHeight(), true);
    juce::Graphics g (image);
    h.panel.paintEntireComponent (g, true);

    REQUIRE (fractionOfNonBackgroundPixels (image) > 0.01f);
    REQUIRE (h.panel.getSummaryText().isNotEmpty());
}

TEST_CASE ("every format is offered", "[ui][render]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PanelHarness h;

    REQUIRE (h.panel.getNumFormats() == 4);
}

TEST_CASE ("the selection scope appears only when there is a selection", "[ui][render]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PanelHarness h;

    REQUIRE (h.panel.getNumScopes() == 2);

    // EditorState broadcasts asynchronously, and there is no message loop being
    // pumped here, so the panel is asked directly.
    h.editorState.setSelectedBarRange ({ 2, 5 });
    h.panel.refresh();
    REQUIRE (h.panel.getNumScopes() == 3);

    // And goes away again, rather than sitting there doing nothing.
    h.editorState.clearBarSelection();
    h.panel.refresh();
    REQUIRE (h.panel.getNumScopes() == 2);
}

TEST_CASE ("choosing the selection scope renders those bars", "[ui][render]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PanelHarness h;

    h.editorState.setSelectedBarRange ({ 2, 5 });
    h.panel.refresh();
    h.panel.setScopeForTesting (RenderPanel::selectionScope);

    const auto request = h.panel.getRequest();

    REQUIRE (request.options.mode == Transport::Mode::song);
    REQUIRE (request.options.barRange.firstBar == 2);
    REQUIRE (request.options.barRange.lastBar == 5);
}

TEST_CASE ("the pattern scope carries the pattern the editor is on", "[ui][render]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PanelHarness h;

    h.editorState.setCurrentPatternId (1);
    h.panel.refresh();
    h.panel.setScopeForTesting (RenderPanel::patternScope);

    const auto request = h.panel.getRequest();

    REQUIRE (request.options.mode == Transport::Mode::pattern);
    REQUIRE (request.options.patternId == 1);
    REQUIRE (request.options.barRange.isEmpty());
}

TEST_CASE ("MIDI hides every control that would be lying", "[ui][render]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PanelHarness h;

    REQUIRE (h.panel.isRowVisible ("RATE"));
    REQUIRE (h.panel.isRowVisible ("DEPTH"));
    REQUIRE (h.panel.isRowVisible ("TAIL"));

    h.panel.setFormatForTesting (RenderFormat::midi);

    // Sample rate, bit depth and a release tail mean nothing in a MIDI file.
    // Greying them would still be showing them.
    REQUIRE_FALSE (h.panel.isRowVisible ("RATE"));
    REQUIRE_FALSE (h.panel.isRowVisible ("DEPTH"));
    REQUIRE_FALSE (h.panel.isRowVisible ("TAIL"));
    REQUIRE_FALSE (h.panel.isRowVisible ("QUALITY"));

    REQUIRE (h.panel.isRowVisible ("SCOPE"));
    REQUIRE (h.panel.isRowVisible ("FORMAT"));
}

TEST_CASE ("MP3 swaps bit depth for a quality setting", "[ui][render]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PanelHarness h;

    if (! OfflineRenderer::isAvailable (RenderFormat::mp3))
        SKIP ("lame is not installed, so MP3 cannot be selected");

    h.panel.setFormatForTesting (RenderFormat::mp3);

    REQUIRE (h.panel.isRowVisible ("QUALITY"));
    REQUIRE_FALSE (h.panel.isRowVisible ("DEPTH"));
    REQUIRE (h.panel.isRowVisible ("RATE"));
}

TEST_CASE ("the panel is as tall as the rows it is showing", "[ui][render]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PanelHarness h;

    const auto forWav = h.panel.getRequiredHeight();

    h.panel.setFormatForTesting (RenderFormat::midi);
    const auto forMidi = h.panel.getRequiredHeight();

    // MIDI has two rows to WAV's eight. One fixed height would either crop the
    // longest format or strand MIDI at the top of an empty box.
    REQUIRE (forMidi < forWav);
    REQUIRE (forMidi > 0);
}

TEST_CASE ("the summary says what is going to be written", "[ui][render]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PanelHarness h;

    REQUIRE (h.panel.getSummaryText().contains ("WAV"));
    REQUIRE (h.panel.getSummaryText().contains ("44100"));

    h.panel.setFormatForTesting (RenderFormat::midi);

    REQUIRE (h.panel.getSummaryText().contains ("MIDI"));
    REQUIRE_FALSE (h.panel.getSummaryText().contains ("44100"));
}

TEST_CASE ("the summary's separators are not mangled", "[ui][render]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PanelHarness h;

    // juce::String's constructor reads a const char* as ASCII while its
    // operator+= reads it as UTF-8, so building this text the wrong way round
    // turns every separator into two characters of mojibake.
    REQUIRE_FALSE (h.panel.getSummaryText().containsChar (juce::juce_wchar (0xc2)));
}

TEST_CASE ("Render hands the caller a request rather than touching a file",
           "[ui][render]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PanelHarness h;

    RenderPanel::Request received;
    bool asked = false;

    h.panel.onRender = [&] (const RenderPanel::Request& request)
    {
        received = request;
        asked = true;
    };

    // Straight through the callback the button calls, since the button itself
    // needs a real click to fire.
    h.panel.onRender (h.panel.getRequest());

    REQUIRE (asked);
    REQUIRE (received.suggestedName.isNotEmpty());
    REQUIRE (received.options.sampleRate > 0.0);

    // A name with no path separators or extension in it, ready for a chooser.
    REQUIRE_FALSE (received.suggestedName.containsChar ('/'));
}

TEST_CASE ("a request reflects the dynamics that were asked for", "[ui][render]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PanelHarness h;

    const auto plain = h.panel.getRequest();

    REQUIRE_FALSE (plain.options.normalize);
    REQUIRE (plain.options.fadeInSeconds == Approx (0.0));
    REQUIRE (plain.options.bitDepth == 24);
    REQUIRE_FALSE (plain.options.floatingPoint);
}
