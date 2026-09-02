#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/design/Tokens.h"

namespace dew
{

/** How a value gets from where it is to where it was told to go. */
enum class Ease
{
    linear,      ///< a crossfade, and very little else
    standard,    ///< cubic in-out: anything that both starts and stops on screen
    decelerate,  ///< cubic out: anything ARRIVING - a panel, a value catching up
    accelerate   ///< cubic in: anything LEAVING
};

/** Pure, and unit-testable on its own. `t` is 0..1. */
float applyEase (Ease, float t) noexcept;

// -----------------------------------------------------------------------------

/** One eased scalar, owned by the widget that draws it.

    Reading it is free and const, so paint() never has to ask a clock: the
    Animator pushes time in and the widget pulls a number out.
*/
class MotionValue
{
public:
    MotionValue() = default;
    explicit MotionValue (float initial) noexcept;

    /** Aims at a new value.

        Aiming at the value it is already aiming at is a NO-OP, which is what
        makes this safe to call from a refresh() that re-states every control on
        every document change - and dew has four of those.
    */
    void animateTo (float target, int durationMs, Ease = Ease::standard) noexcept;

    /** Arrives now. What reduce-motion does, and what a control built from a
        document does with the first value it is ever given. */
    void snapTo (float) noexcept;

    float get() const noexcept       { return current; }
    float getTarget() const noexcept { return target; }
    bool  isMoving() const noexcept  { return elapsedMs < durationMs; }

    /** Advances by a wall-clock delta; true if the value changed, so a caller
        repaints only what moved.

        Accumulates ELAPSED time and re-evaluates ease(elapsed/duration). It
        deliberately does not apply a per-frame coefficient - that is the bug
        dew's three meters have, and it means the same animation lands somewhere
        different whenever a frame is dropped.
    */
    bool advance (int deltaMs) noexcept;

private:
    float from = 0.0f, current = 0.0f, target = 0.0f;
    int elapsedMs = 0, durationMs = 0;
    Ease ease = Ease::standard;
};

// -----------------------------------------------------------------------------

/** The one clock.

    Every eased value in dew is stepped from here, so nothing has to own a Timer
    to move and everything in a frame is computed against the same instant.
    Before this there was exactly one animation in the whole application - a
    menu fading in - and sixteen state changes that were hard cuts.

    A juce::Timer rather than a VBlankAttachment, and that is not laziness: a
    vblank needs a ComponentPeer, and every UI test in dew paints into an Image
    with no peer and no message loop. A design that cannot run where the suite
    runs is a design the suite cannot check.
*/
class Animator : private juce::Timer
{
public:
    static Animator& shared();

    struct Client
    {
        virtual ~Client() = default;

        /** Advance by deltaMs, and return whether this client still needs
            stepping.

            Two different questions live near each other here and are worth
            keeping apart. MotionValue::advance answers "did the number change",
            which is what decides a repaint - including the final frame, where
            it changes and then stops. This answers "is there more to come",
            which is what decides whether the clock keeps running. A client that
            returned the first would keep the timer alive for one extra frame
            after everything had settled.
        */
        virtual bool advanceAnimation (int deltaMs) = 0;
    };

    void addClient (Client&);
    void removeClient (Client&);   ///< every Client MUST call this in its destructor

    /** Animation is OPT-IN, and off unless the application turns it on.

        Deliberately the wrong way round from how it looks. Every headless test
        and every dew_shot render then behaves exactly as it did before this
        class existed - DesignSystemTests sets a knob twice and asserts the
        second render, and there is no version of "animate by default" that
        leaves that test meaning what it means. Motion is a property of a
        running application, not of a widget.
    */
    void setEnabled (bool);
    bool isEnabled() const noexcept { return enabled; }

    /** Turns every transition instant. Not "slower": off. A duration of zero
        makes animateTo identical to snapTo, so no call site needs a branch. */
    void setReduceMotion (bool);
    bool getReduceMotion() const noexcept { return reduceMotion; }

    /** What MotionValue asks before it decides to move at all. */
    bool motionIsOn() const noexcept { return enabled && ! reduceMotion; }

    /** THE TEST SEAM.

        Steps every registered client by exactly this many milliseconds, with no
        message loop and no wall clock. The timer does the same thing with a
        measured delta; a test does it with a chosen one, so a whole interaction
        can be walked frame by frame rather than sampled at its ends.
    */
    void advance (int deltaMs);

    bool isAnimating() const noexcept { return ! clients.isEmpty() && anyMoving; }

    /** Stops the clock without disabling motion, so a test drives time itself.
        A headless test has no message loop for the timer to tick on anyway;
        this makes that explicit rather than accidental. */
    void stopTimerForTesting() { stopTimer(); }

private:
    void timerCallback() override;

    juce::Array<Client*> clients;
    juce::int64 lastTickMs = 0;
    bool enabled = false;
    bool reduceMotion = false;
    bool anyMoving = false;
};

/** Motion on and driven by hand, for the lifetime of the object. What an
    animation test builds; everything else stays instant. */
struct ScopedAnimation
{
    ScopedAnimation();
    ~ScopedAnimation();

private:
    bool wasEnabled;
};

/** Reduce motion on, for the lifetime of the object. */
struct ScopedReduceMotion
{
    ScopedReduceMotion();
    ~ScopedReduceMotion();

private:
    bool wasReduced;
};

} // namespace dew
