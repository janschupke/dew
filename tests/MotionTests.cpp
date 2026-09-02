#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "PaintProbe.h"
#include "ui/DewLookAndFeel.h"
#include "ui/design/Animator.h"
#include "ui/design/Tokens.h"
#include "ui/primitives/DewControls.h"

using namespace dew;
using namespace dew::tokens;
using Catch::Approx;


namespace
{

/** How far round a knob's arc has swept, measured from what it draws.

    A needle position is not readable from the outside - the value is the
    document's, not the widget's - so this asks the pixels, which is the only
    thing a user can see either.
*/
float renderKnobArc (DewKnob& knob)
{
    return testing::coverageOf (testing::render (knob), tokens::colour::accent) * 20.0f;
}

} // namespace

TEST_CASE ("easing is a pure function that spans nought to one", "[motion]")
{
    for (const auto ease : { Ease::linear, Ease::standard, Ease::decelerate, Ease::accelerate })
    {
        REQUIRE (applyEase (ease, 0.0f) == Approx (0.0f).margin (1e-6));
        REQUIRE (applyEase (ease, 1.0f) == Approx (1.0f).margin (1e-6));

        // Monotone. An ease that overshoots reads as a wobble, which on a
        // filter cutoff is audible rather than decorative.
        auto previous = -1.0f;

        for (int i = 0; i <= 20; ++i)
        {
            const auto value = applyEase (ease, (float) i / 20.0f);
            REQUIRE (value >= previous - 1.0e-6f);
            previous = value;
        }

        // Out of range is clamped, not extrapolated.
        REQUIRE (applyEase (ease, -1.0f) == Approx (0.0f).margin (1e-6));
        REQUIRE (applyEase (ease, 2.0f) == Approx (1.0f).margin (1e-6));
    }
}

TEST_CASE ("a value moves rather than jumping, and arrives exactly", "[motion]")
{
    ScopedAnimation animation;   // this test opts in; nothing else does

    MotionValue value { 0.0f };
    value.animateTo (1.0f, motion::valueMs, Ease::decelerate);

    juce::Array<float> path;

    for (int elapsed = 0; elapsed < motion::valueMs; elapsed += 10)
    {
        path.add (value.get());
        value.advance (10);
    }

    value.advance (motion::valueMs);   // well past the end

    REQUIRE (path.size() > 8);
    CHECK (path.getFirst() == Approx (0.0f).margin (1e-6));

    // Exactly, not nearly: a knob that settles at 0.9997 shows it.
    CHECK (juce::exactlyEqual (value.get(), 1.0f));

    for (int i = 1; i < path.size(); ++i)
        CHECK (path[i] >= path[i - 1] - 1.0e-6f);

    // And it MOVED in between, rather than snapping on the last frame - which
    // is what a naive "is it there yet" test would have accepted.
    const auto middle = path[path.size() / 2];
    INFO ("midpoint of the path: " << middle);
    CHECK (middle > 0.2f);
    CHECK (middle < 1.0f);
}

TEST_CASE ("motion arrives in the same place however it is stepped", "[motion]")
{
    // Path independence. A per-frame coefficient - which is what all three of
    // dew's meters use - lands somewhere different when a frame is dropped, and
    // that is the bug this design exists to not have.
    ScopedAnimation animation;

    MotionValue coarse { 0.0f }, fine { 0.0f };
    coarse.animateTo (1.0f, motion::panelMs, Ease::decelerate);
    fine.animateTo (1.0f, motion::panelMs, Ease::decelerate);

    coarse.advance (motion::panelMs / 2);

    for (int i = 0; i < 9; ++i)
        fine.advance (motion::panelMs / 18);

    INFO ("one step " << coarse.get() << " vs nine " << fine.get());
    CHECK (std::abs (coarse.get() - fine.get()) < 1.0e-4f);
}

TEST_CASE ("aiming where it is already going does not restart it", "[motion]")
{
    // The panels re-state every control on every document change, so animateTo
    // is called with the same target many times while a value is in flight.
    // Restarting each time would leave a knob permanently a frame behind.
    ScopedAnimation animation;

    MotionValue value { 0.0f };
    value.animateTo (1.0f, motion::valueMs, Ease::decelerate);
    value.advance (motion::valueMs / 2);

    const auto halfway = value.get();

    value.animateTo (1.0f, motion::valueMs, Ease::decelerate);
    value.advance (0);

    CHECK (value.get() == Approx (halfway));

    value.advance (motion::valueMs / 2 + 1);
    CHECK (juce::exactlyEqual (value.get(), 1.0f));
}

TEST_CASE ("reduce motion makes every transition instant", "[motion]")
{
    ScopedAnimation animation;
    ScopedReduceMotion reduced;

    MotionValue value { 0.0f };
    value.animateTo (1.0f, motion::panelMs, Ease::standard);

    // Before any advance at all.
    CHECK (juce::exactlyEqual (value.get(), 1.0f));
    CHECK_FALSE (value.isMoving());
}

TEST_CASE ("nothing moves unless the application turns motion on", "[motion]")
{
    // The default, and the reason every other test in the suite still means
    // what it meant: a widget built in a headless test snaps exactly as it did
    // before there was an animator.
    REQUIRE_FALSE (Animator::shared().isEnabled());

    MotionValue value { 0.0f };
    value.animateTo (1.0f, motion::panelMs, Ease::standard);

    CHECK (juce::exactlyEqual (value.get(), 1.0f));
    CHECK_FALSE (value.isMoving());
}

namespace
{

struct CountingClient : Animator::Client
{
    CountingClient() { Animator::shared().addClient (*this); }
    ~CountingClient() override { Animator::shared().removeClient (*this); }

    bool advanceAnimation (int deltaMs) override
    {
        total += deltaMs;
        ++ticks;

        // A widget repaints on `moved` and tells the animator whether to keep
        // the clock running, which is the distinction the Client doc draws.
        moved = value.advance (deltaMs);
        return value.isMoving();
    }

    MotionValue value { 0.0f };
    int total = 0, ticks = 0;
    bool moved = false;
};

} // namespace

TEST_CASE ("the animator steps its clients by exactly the time it is given", "[motion]")
{
    ScopedAnimation animation;

    CountingClient client;
    client.value.animateTo (1.0f, motion::panelMs, Ease::linear);

    Animator::shared().advance (30);
    Animator::shared().advance (30);

    CHECK (client.ticks == 2);
    CHECK (client.total == 60);
    CHECK (client.value.get() == Approx (60.0f / (float) motion::panelMs).margin (1e-4));

    // And it knows when it is at rest, so the timer can stop.
    Animator::shared().advance (motion::panelMs);
    CHECK_FALSE (Animator::shared().isAnimating());
}

TEST_CASE ("a client that is removed stops being stepped", "[motion]")
{
    ScopedAnimation animation;

    {
        CountingClient client;
        Animator::shared().advance (10);
        REQUIRE (client.ticks == 1);
    }

    // Destroyed, and deregistered by its own destructor. Stepping again must
    // not walk a dangling pointer.
    Animator::shared().advance (10);
    SUCCEED ("stepping after a client was destroyed did not crash");
}

TEST_CASE ("a menu simply appears when motion is reduced", "[motion][dropdown]")
{
    // The counterpart to DesignSystemTests' "a menu opens by animating rather
    // than appearing", which still passes because reduce motion is off by
    // default. The popup rise predates the animator and uses JUCE's desktop
    // animator - so reduce motion has to reach it separately, and this is what
    // says it does.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    DewLookAndFeel lookAndFeel;

    juce::Component window;
    window.setBounds (100, 100, 180, 90);

    const auto target = window.getBounds();

    {
        ScopedReduceMotion reduced;
        lookAndFeel.preparePopupMenuWindow (window);
    }

    CHECK (window.getBounds() == target);
    CHECK (window.getAlpha() > 0.99f);
    CHECK_FALSE (juce::Desktop::getInstance().getAnimator().isAnimating (&window));
}

TEST_CASE ("a knob sweeps to a new value and arrives exactly on it", "[motion][knob]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    ScopedAnimation animating;

    DewKnob knob ("CUTOFF", 0.0, 1.0, 0.001);
    knob.setSize (72, 78);
    knob.resized();

    // The first value a knob is ever given SNAPS. A panel built from a document
    // must not sweep every knob up from zero.
    knob.setValue (0.25, juce::dontSendNotification);

    const auto atStart = renderKnobArc (knob);

    knob.setValue (1.0, juce::dontSendNotification);

    // Still where it was, because nothing has advanced the clock.
    REQUIRE (renderKnobArc (knob) == Approx (atStart).margin (0.005));

    // Sampled across the WHOLE sweep, not at its ends: a naive "is it there
    // yet" test passes on a widget that does nothing for 119ms and then jumps.
    auto previous = atStart;
    auto sawMiddle = false;

    for (int step = 0; step < 12; ++step)
    {
        Animator::shared().advance (motion::valueMs / 12);

        const auto now = renderKnobArc (knob);
        REQUIRE (now >= previous - 0.01f);      // monotone

        if (now > atStart + 0.05f && now < 1.0f - 0.05f)
            sawMiddle = true;

        previous = now;
    }

    INFO ("started at " << atStart << ", ended at " << previous);
    CHECK (sawMiddle);
    CHECK (previous > atStart);

    // And it ARRIVES, rather than stopping near the target.
    Animator::shared().advance (motion::valueMs);
    CHECK_FALSE (Animator::shared().isAnimating());
}

TEST_CASE ("a knob is not eased against the pointer dragging it", "[motion][knob]")
{
    // Easing a control against the hand moving it feels broken: the needle
    // trails the pointer the whole way and lands late.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    ScopedAnimation animating;

    DewKnob knob ("CUTOFF", 0.0, 1.0, 0.001);
    knob.setSize (72, 78);
    knob.resized();
    knob.setValue (0.0, juce::dontSendNotification);

    // What DewKnob wires to the slider's own drag callbacks.
    knob.getSlider().onDragStart();
    knob.getSlider().setValue (0.9, juce::sendNotificationSync);

    // No advance, and it is already there.
    CHECK (renderKnobArc (knob) > 0.5f);

    knob.getSlider().onDragEnd();
}

TEST_CASE ("reduce motion leaves a knob exactly where it was told", "[motion][knob]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    ScopedAnimation animating;
    ScopedReduceMotion reduced;

    DewKnob knob ("CUTOFF", 0.0, 1.0, 0.001);
    knob.setSize (72, 78);
    knob.resized();

    knob.setValue (0.0, juce::dontSendNotification);
    const auto atStart = renderKnobArc (knob);

    knob.setValue (1.0, juce::dontSendNotification);

    // Before any advance at all - reduce motion is not "faster", it is off.
    CHECK (renderKnobArc (knob) > atStart + 0.2f);
    CHECK_FALSE (Animator::shared().isAnimating());
}

TEST_CASE ("a button lifts into a hover rather than cutting to it", "[motion][button]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    ScopedAnimation animating;

    DewButton button ("Render", DewButton::Role::normal);
    button.setSize (90, size::controlHeight);

    const auto atRest = testing::meanBrightness (testing::render (button));

    // What juce::Button calls when the pointer arrives.
    button.setState (juce::Button::buttonOver);

    // Not yet: the lift is a transition, and no time has passed.
    CHECK (testing::meanBrightness (testing::render (button)) == Approx (atRest).margin (0.002));

    Animator::shared().advance (motion::quickMs);

    const auto hovered = testing::meanBrightness (testing::render (button));
    INFO ("at rest " << atRest << ", hovered " << hovered);
    CHECK (hovered > atRest);

    // A press lifts further than a hover, which is the whole point of having
    // two rungs rather than one.
    button.setState (juce::Button::buttonDown);
    Animator::shared().advance (motion::quickMs);

    CHECK (testing::meanBrightness (testing::render (button)) > hovered);
}
