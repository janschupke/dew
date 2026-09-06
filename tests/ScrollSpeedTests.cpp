#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "TestSupport.h"
#include "engine/AudioEngine.h"
#include "app/ProjectDocument.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "ui/ChannelRackComponent.h"
#include "ui/EditorState.h"
#include "ui/design/Gestures.h"
#include "ui/PianoRollComponent.h"
#include "ui/PlaylistComponent.h"
#include "ui/ScoreEditorComponent.h"

using namespace dew;

namespace
{

/** One notch, straight down, with natural scrolling off - so the number a test
    reads is the number a handler was given.
*/
juce::MouseWheelDetails notch (float y)
{
    juce::MouseWheelDetails wheel;
    wheel.deltaX = 0.0f;
    wheel.deltaY = y;
    wheel.isReversed = false;
    wheel.isSmooth = false;
    wheel.isInertial = false;
    return wheel;
}

juce::MouseEvent eventOn (juce::Component& target, juce::Point<int> local,
                          juce::ModifierKeys mods = juce::ModifierKeys())
{
    return testing::mouseEventAt (target, local.toFloat(), mods);
}

/** How far one notch moves, whatever the view measures in, converted back to
    pixels by the view's own mapping. */
constexpr float oneNotch = 1.0f;

} // namespace

TEST_CASE ("one wheel notch travels the same distance in every view", "[ui][gesture][scroll]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    // The defect: a notch meant six different distances. Six steps horizontally,
    // which is 18px zoomed out and 720px zoomed in; one lane down the playlist,
    // which is 34px or 204px; three rows down the roll, which is 42px; and
    // whatever JUCE picked in the two views that handled nothing.
    const auto expected = gesture::wheelPixelsPerNotch;

    ProjectDocument document;
    document.setState (ProjectFactory::createDefault(), true);

    AudioEngine engine;
    EditorState editorState;

    // --- the piano roll, vertically -----------------------------------------
    {
        PianoRollComponent roll { document, engine, editorState };
        roll.setSize (1200, 700);
        roll.setVisible (true);
        roll.refresh();
        roll.resized();
        roll.centreOnPitch (66);

        double zoom = 0.0, scroll = 0.0, before = 0.0;
        roll.captureView (zoom, scroll, before);

        roll.mouseWheelMove (eventOn (roll, roll.getNoteArea().getCentre()), notch (-oneNotch));

        double after = 0.0;
        roll.captureView (zoom, scroll, after);

        INFO ("piano roll pitch scroll");
        CHECK (juce::approximatelyEqual (after - before, expected));
    }

    // --- the playlist, vertically -------------------------------------------
    {
        PlaylistComponent playlist { document, engine, editorState };
        playlist.setSize (1200, 500);
        playlist.setVisible (true);
        playlist.refresh();
        playlist.resized();

        // Enough lanes that there is a notch's worth of room to scroll into. A
        // view the content already fits pins to zero, which would make this
        // pass for the wrong reason.
        for (int i = 0; i < 40; ++i)
            playlist.addTrack();

        const auto before = playlist.getTrackScrollPx();

        playlist.mouseWheelMove (eventOn (playlist, { 600, 300 }), notch (-oneNotch));

        INFO ("playlist track scroll");
        CHECK (juce::approximatelyEqual (playlist.getTrackScrollPx() - before, expected));
    }

    // --- and horizontally, at two zooms, in the roll ------------------------
    {
        // Narrow, so a sixteen-step pattern overflows it at both zooms below.
        // clampScroll pins a view wider than its material to zero, and a test
        // measuring a clamp would report whatever the clamp happened to be.
        PianoRollComponent roll { document, engine, editorState };
        roll.setSize (400, 700);
        roll.setVisible (true);
        roll.refresh();
        roll.resized();

        const auto travelInPixels = [&roll]
        {
            const auto before = roll.getTimeline().scrollOffsetSteps;

            roll.mouseWheelMove (
                eventOn (roll, roll.getNoteArea().getCentre(), juce::ModifierKeys::shiftModifier),
                notch (-oneNotch));

            return (roll.getTimeline().scrollOffsetSteps - before)
                   * roll.getTimeline().pixelsPerStep;
        };

        roll.applyView (40.0, 0.0, 0.0);

        INFO ("piano roll time scroll, zoomed out");
        CHECK (juce::approximatelyEqual (travelInPixels(), expected));

        // THE thing that made the horizontal axis feel wrong: it counted steps,
        // so the same flick travelled three times further at three times the
        // zoom - and forty times further across the whole range.
        roll.applyView (TimelineView::maxPixelsPerStep, 0.0, 0.0);

        INFO ("piano roll time scroll, zoomed in");
        CHECK (juce::approximatelyEqual (travelInPixels(), expected));
    }
}

TEST_CASE ("the channel rack's viewport already agrees, and has to keep agreeing",
           "[ui][gesture][scroll]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    // The rack scrolls through a juce::Viewport, which moves by fourteen times
    // its own single step. wheelPixelsPerNotch is that number, so the rack needs
    // no override - but "needs none" is only true while JUCE keeps its two
    // constants, and that is a fact worth a test rather than a comment. The
    // colour ramp is held the same way, for the same reason.
    juce::Viewport viewport;
    juce::Component content;

    content.setSize (400, 4000);
    viewport.setViewedComponent (&content, false);
    viewport.setSize (400, 400);
    viewport.setScrollBarsShown (true, false);

    REQUIRE (viewport.getViewPositionY() == 0);

    viewport.mouseWheelMove (eventOn (viewport, { 200, 200 }), notch (-oneNotch));

    INFO ("juce::Viewport moved " << viewport.getViewPositionY() << "px for one notch");
    CHECK (juce::exactlyEqual ((double) viewport.getViewPositionY(), gesture::wheelPixelsPerNotch));
}

TEST_CASE ("the score's text scrolls in whole lines, and loses no fraction of one",
           "[ui][gesture][scroll]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    ScoreEditorComponent editor { document };
    editor.setSize (900, 400);
    editor.resized();

    auto& text = editor.getEditor();

    REQUIRE (text.getLineHeight() > 0);
    REQUIRE (text.getFirstLineOnScreen() == 0);

    const auto expectedLines = (int) std::trunc (gesture::wheelPixelsPerNotch
                                                 / (double) text.getLineHeight());
    REQUIRE (expectedLines > 0);

    text.mouseWheelMove (eventOn (text, { 400, 200 }), notch (-oneNotch));
    CHECK (text.getFirstLineOnScreen() == expectedLines);

    // A trackpad sends fractions of a line. Rounding each event on its own means
    // a slow drag scrolls nothing at all, for ever - so the remainder carries.
    text.scrollToLine (0);
    REQUIRE (text.getFirstLineOnScreen() == 0);

    for (int i = 0; i < 20; ++i)
        text.mouseWheelMove (eventOn (text, { 400, 200 }), notch (-oneNotch / 20.0f));

    CHECK (text.getFirstLineOnScreen() == expectedLines);
}
