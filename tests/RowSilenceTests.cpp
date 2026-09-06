// What a row that has been turned OFF looks like.
//
// Its own file rather than more of SelectionTests.cpp, which is already at the
// four hundred code lines the tree holds every file to - and it is a subject of
// its own: silence::applyTo is one rule with three call sites, and it was three
// answers of which two were "nothing".

#include <cmath>

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ControlWalkHarness.h"
#include "engine/AudioEngine.h"
#include "app/ProjectDocument.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "ui/ChannelRackComponent.h"
#include "ui/EditorState.h"
#include "ui/MixerComponent.h"
#include "ui/design/Tokens.h"
#include "FixtureProject.h"

using namespace dew;
using dew::testing::findDescendantWithID;

namespace
{

void collect (juce::Component& root, juce::Array<juce::Component*>& out)
{
    for (auto* child : root.getChildren())
    {
        out.add (child);
        collect (*child, out);
    }
}

} // namespace

TEST_CASE ("a muted row dims, controls included, in all three views", "[ui][selection][silence]")
{
    /*  It was three answers and two of them were "nothing". The rack dimmed the
        STEPS and left the row alone; the playlist drew a scrim over its
        header's ground, which a child component paints on top of, so the name
        and the power glyph stayed at full strength; and the mixer dimmed
        nothing at all.

        silence::applyTo is the shared half a scrim cannot do. Asserted on the
        controls rather than on pixels, because that is the half that was
        missing everywhere - and in BOTH directions, since a row that dimmed and
        never came back would pass a one-way check.
    */
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    AudioEngine engine;
    EditorState editorState;

    document.setState (dew::testing::fixtureProject(), true);

    /** Every control on a row except the one that turns it back on, which stays
        bright deliberately: the way out of a state must not be drawn in it. */
    const auto dimmedControlsIn = [] (juce::Component& row, const juce::String& toggleId)
    {
        juce::Array<juce::Component*> all;
        collect (row, all);

        auto dimmed = 0, bright = 0, toggles = 0;

        for (auto* c : all)
        {
            if (c->getComponentID() == toggleId)
            {
                ++toggles;
                CHECK (juce::exactlyEqual (c->getAlpha(), 1.0f));
                continue;
            }

            // Only the row's own children carry the alpha; what is inside them
            // is covered by their parent.
            if (c->getParentComponent() != &row)
                continue;

            if (c->getAlpha() < 1.0f)
                ++dimmed;
            else
                ++bright;
        }

        INFO (dimmed << " dimmed, " << bright << " bright, " << toggles << " toggles");
        CHECK (toggles == 1);

        return dimmed;
    };

    SECTION ("a channel rack row")
    {
        ChannelRackComponent rack { document, engine, editorState };
        rack.setSize (1000, 600);
        rack.setVisible (true);
        rack.refresh();
        rack.resized();

        // Found FRESH each time: ChannelRackComponent::refresh rebuilds every
        // row, so a pointer taken before it is a pointer to a destroyed one.
        const auto firstHeader = [&rack]() -> juce::Component&
        {
            juce::Array<juce::Component*> all;
            collect (rack, all);

            for (auto* c : all)
                if (c->getComponentID() == "channelHeader")
                    return *c;

            FAIL ("no channel header");
            return rack;
        };

        auto channel = ProjectEdits::findChannel (document.getState(), 1);
        REQUIRE (channel.isValid());

        // The control case first: an unmuted row has nothing dimmed on it.
        CHECK (dimmedControlsIn (firstHeader(), "channelEnabled") == 0);

        ProjectEdits::setProperty (channel, ids::muted, true, nullptr, "mute");
        rack.refresh();

        CHECK (dimmedControlsIn (firstHeader(), "channelEnabled") > 0);

        // And back, so the dim is a state rather than a one-way door.
        ProjectEdits::setProperty (channel, ids::muted, false, nullptr, "unmute");
        rack.refresh();

        CHECK (dimmedControlsIn (firstHeader(), "channelEnabled") == 0);
    }

    SECTION ("a mixer strip")
    {
        MixerComponent mixer { document, editorState };
        mixer.setSize (1000, 600);
        mixer.setVisible (true);
        mixer.refresh();
        mixer.resized();

        juce::Array<juce::Component*> all;
        collect (mixer, all);

        juce::Component* strip = nullptr;

        for (auto* c : all)
            if (c->getComponentID() == "mixerStrip" && findDescendantWithID (*c, "stripEnabled"))
            {
                strip = c;
                break;
            }

        REQUIRE (strip != nullptr);

        juce::ValueTree track;

        for (const auto& child : document.getState().getChildWithName (ids::MIXER))
            if (child.hasType (ids::MIXER_TRACK))
            {
                track = child;
                break;
            }

        REQUIRE (track.isValid());

        CHECK (dimmedControlsIn (*strip, "stripEnabled") == 0);

        ProjectEdits::setProperty (track, ids::mute, true, nullptr, "mute");

        CHECK (dimmedControlsIn (*strip, "stripEnabled") > 0);

        ProjectEdits::setProperty (track, ids::mute, false, nullptr, "unmute");

        CHECK (dimmedControlsIn (*strip, "stripEnabled") == 0);
    }
}

TEST_CASE ("a lane's colour survives being turned off", "[ui][silence][design]")
{
    /*  The other half of the same rule, and the half that was missed twice.

        `dew::silence` gave a muted channel, lane and insert one presentation -
        and landed on the three row HEADERS. The playlist's clips kept their own
        answer, which was `emphasis::silenced` SETTING saturation to 0.1: every
        muted clip in the arrangement came out the same grey whatever colour its
        lane was, so lane colouring stopped meaning anything on exactly the
        lanes that had been switched off.

        Asserted on the pure function rather than on pixels, because that is
        where the rule lives - all three clip painters in PlaylistPaint.cpp read
        it, and a pixel test would only cover whichever one it rendered.
    */
    using namespace tokens;

    constexpr int rampSize = (int) (sizeof (colour::channelRamp) / sizeof (colour::channelRamp[0]));

    for (int i = 0; i < rampSize; ++i)
    {
        const auto lane = colour::channelRamp[i];
        const auto off = emphasis::silenced (lane);

        INFO ("ramp entry " << i << ": " << lane.toString() << " -> " << off.toString());

        // The hue is the identity. It is what makes the clip THIS lane's clip
        // and not some other one, so turning the lane off may not spend it.
        CHECK (std::abs (off.getHue() - lane.getHue()) < 0.02f);

        // Drained, but not to grey. The old value was a flat 0.1 for every
        // input, which is the number this guards against coming back.
        CHECK (off.getSaturation() > 0.3f);

        // And still reads as OFF: quieter than the lane that is playing.
        CHECK (off.getBrightness() < lane.getBrightness());
    }

    // The complaint itself, stated directly: eight lanes turned off are still
    // eight distinguishable colours, not eight copies of one grey.
    for (int i = 0; i < rampSize; ++i)
        for (int j = i + 1; j < rampSize; ++j)
        {
            const auto a = emphasis::silenced (colour::channelRamp[i]);
            const auto b = emphasis::silenced (colour::channelRamp[j]);

            INFO ("silenced ramp entries " << i << " and " << j << ": " << a.toString() << " and "
                                           << b.toString());

            // Per channel, and deliberately the same tolerance PaintProbe's
            // coverageOf uses to decide two colours are the same one.
            CHECK ((std::abs ((int) a.getRed() - (int) b.getRed()) >= 24
                    || std::abs ((int) a.getGreen() - (int) b.getGreen()) >= 24
                    || std::abs ((int) a.getBlue() - (int) b.getBlue()) >= 24));
        }
}
