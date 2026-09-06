// The keyboard strip down the left of the roll, and what lights on it.
//
// Its own file rather than another case in PianoRollViewTests.cpp, which is at
// the length the tree allows one, and because these need a running engine while
// nothing else about the view does. The fixture is RollHarness.h.

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/ProjectEdits.h"
#include "ui/design/Tokens.h"

#include "PaintProbe.h"
#include "RollHarness.h"

using namespace dew;
using namespace dew::testing;

namespace
{

/** How much of the KEYBOARD STRIP is painted in the accent colour.

    The strip alone, not the whole roll: the accent is the playhead's colour too
    and the notes carry the channel's, so measuring the component would answer a
    different question on every project.
*/
float litFractionOf (RollHarness& h)
{
    const auto image = render (h.roll).getClippedImage (h.roll.getKeyboardArea());
    return coverageOf (image, tokens::colour::accent);
}

/** Blocks enough for a preview note to be started and reported. */
void runEngine (RollHarness& h, int blocks = 3)
{
    juce::AudioBuffer<float> block (2, 512);

    for (int i = 0; i < blocks; ++i)
    {
        block.clear();
        h.engine.processBlock (block);
    }
}

} // namespace

TEST_CASE ("the keyboard strip lights the keys that are sounding", "[ui][pianoroll][keys]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    h.engine.prepare (44100.0, 512);
    h.engine.setProject (h.document.getState());

    runEngine (h);
    h.roll.refreshSoundingKeys();

    const auto dark = litFractionOf (h);

    // Through the PREVIEW ring, which is the path the typing keyboard takes -
    // so this asserts the case that has no mouse and no playhead behind it, and
    // is the one a display watching only its own gestures would miss.
    const auto channelIndex = ProjectEdits::channelIndexForId (
        h.document.getState(), h.editorState.getSelectedChannelId());
    REQUIRE (channelIndex >= 0);

    h.engine.previewNoteOn (channelIndex, 66, 0.9f);
    runEngine (h);

    h.roll.refreshSoundingKeys();

    const auto lit = litFractionOf (h);

    INFO ("dark " << dark << ", lit " << lit);
    CHECK (lit > dark);

    // ...and out again when the note ends, rather than staying lit until
    // something else happens to repaint.
    h.engine.previewNoteOff (channelIndex, 66);
    runEngine (h, 200);

    h.roll.refreshSoundingKeys();

    CHECK (litFractionOf (h) <= dark);
}

TEST_CASE ("a key sounding on another channel does not light this one", "[ui][pianoroll][keys]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    h.engine.prepare (44100.0, 512);
    h.engine.setProject (h.document.getState());

    runEngine (h);
    h.roll.refreshSoundingKeys();

    const auto dark = litFractionOf (h);

    const auto selected = ProjectEdits::channelIndexForId (h.document.getState(),
                                                           h.editorState.getSelectedChannelId());
    REQUIRE (selected == 0);

    // The roll shows one channel at a time, so it must ask about that one. A
    // strip that lit for anything sounding anywhere would be lit constantly
    // while a song played.
    h.engine.previewNoteOn (2, 66, 0.9f);
    runEngine (h);

    h.roll.refreshSoundingKeys();

    CHECK (litFractionOf (h) <= dark);
}
