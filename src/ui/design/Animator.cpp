#include "ui/design/Animator.h"

namespace dew
{

float applyEase (Ease ease, float t) noexcept
{
    const auto x = juce::jlimit (0.0f, 1.0f, t);

    switch (ease)
    {
        case Ease::linear:     return x;
        case Ease::accelerate: return x * x * x;
        case Ease::decelerate: { const auto inv = 1.0f - x; return 1.0f - inv * inv * inv; }
        case Ease::standard:
            return x < 0.5f ? 4.0f * x * x * x
                            : 1.0f - std::pow (-2.0f * x + 2.0f, 3.0f) * 0.5f;
    }

    return x;
}

// -----------------------------------------------------------------------------

MotionValue::MotionValue (float initial) noexcept
    : from (initial), current (initial), target (initial)
{
}

void MotionValue::animateTo (float newTarget, int newDurationMs, Ease newEase) noexcept
{
    // Already going there. Not a cheap guard but a correctness one: the panels
    // re-state every control on every document change, and restarting an
    // animation each time would leave a knob permanently a frame behind.
    if (juce::exactlyEqual (newTarget, target) && isMoving())
        return;

    if (juce::exactlyEqual (newTarget, current) && ! isMoving())
        return;

    if (! Animator::shared().motionIsOn() || newDurationMs <= 0)
    {
        snapTo (newTarget);
        return;
    }

    from = current;
    target = newTarget;
    durationMs = newDurationMs;
    elapsedMs = 0;
    ease = newEase;
}

void MotionValue::snapTo (float value) noexcept
{
    from = current = target = value;
    elapsedMs = 0;
    durationMs = 0;
}

bool MotionValue::advance (int deltaMs) noexcept
{
    if (! isMoving())
        return false;

    elapsedMs = juce::jmin (durationMs, elapsedMs + juce::jmax (0, deltaMs));

    const auto t = durationMs > 0 ? (float) elapsedMs / (float) durationMs : 1.0f;
    const auto previous = current;

    // Exactly the target at the end. An ease that lands a hair short would
    // leave a knob reading 0.9997 forever, which a test comparing exactly - and
    // a person reading the number under it - would both notice.
    current = elapsedMs >= durationMs ? target
                                      : from + (target - from) * applyEase (ease, t);

    return ! juce::exactlyEqual (current, previous);
}

// -----------------------------------------------------------------------------

Animator& Animator::shared()
{
    static Animator instance;
    return instance;
}

void Animator::addClient (Client& client)
{
    clients.addIfNotAlreadyThere (&client);

    if (enabled && ! isTimerRunning())
    {
        lastTickMs = juce::Time::currentTimeMillis();
        startTimerHz (tokens::motion::playheadHz);
    }
}

void Animator::removeClient (Client& client)
{
    clients.removeAllInstancesOf (&client);

    if (clients.isEmpty())
        stopTimer();
}

void Animator::setEnabled (bool shouldBeEnabled)
{
    enabled = shouldBeEnabled;

    if (! enabled)
    {
        stopTimer();
        return;
    }

    if (! clients.isEmpty() && ! isTimerRunning())
    {
        lastTickMs = juce::Time::currentTimeMillis();
        startTimerHz (tokens::motion::playheadHz);
    }
}

void Animator::setReduceMotion (bool shouldReduce)
{
    reduceMotion = shouldReduce;
}

void Animator::advance (int deltaMs)
{
    anyMoving = false;

    // A copy: a client may remove itself while being stepped, and a widget
    // whose animation finished is exactly the one likely to.
    const auto stepping = clients;

    for (auto* client : stepping)
        if (clients.contains (client))
            anyMoving = client->advanceAnimation (deltaMs) || anyMoving;
}

void Animator::timerCallback()
{
    const auto now = juce::Time::currentTimeMillis();
    const auto delta = (int) juce::jlimit ((juce::int64) 0, (juce::int64) 250, now - lastTickMs);
    lastTickMs = now;

    advance (delta);
}

// -----------------------------------------------------------------------------

ScopedAnimation::ScopedAnimation()
    : wasEnabled (Animator::shared().isEnabled())
{
    Animator::shared().setEnabled (true);

    // Enabled, but driven by hand: a test steps time with advance() rather than
    // waiting on a message loop it does not have.
    Animator::shared().stopTimerForTesting();
}

ScopedAnimation::~ScopedAnimation()
{
    Animator::shared().setEnabled (wasEnabled);
}

ScopedReduceMotion::ScopedReduceMotion()
    : wasReduced (Animator::shared().getReduceMotion())
{
    Animator::shared().setReduceMotion (true);
}

ScopedReduceMotion::~ScopedReduceMotion()
{
    Animator::shared().setReduceMotion (wasReduced);
}

} // namespace dew
