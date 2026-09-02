#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace dew::meter
{

/** How dew's level meters move and how they are scaled.

    Three widgets showed a level - the mixer strip, the audio settings input
    meter and the signal scope's spectrum - and each had its own answer to both
    questions.

    The ballistics were a per-tick coefficient: 0.82 in two of them and 0.8 in
    the third. A coefficient locks a meter to the rate it was tuned at, and the
    scope's own comment says so ("the coefficient is per tick") rather than
    fixing it. It also did not do what it said: "fall over about a third of a
    second" at 0.82 a tick, thirty times a second, is a sixth of a second. The
    time constant is the declared value, so the meters now fall at the speed
    they always claimed to.

    The scale was dB in the mixer and linear everywhere else, which is why the
    settings meter sat in its bottom fifth on a healthy signal and read as
    broken.
*/

/** Rise instantly, fall on a time constant.

    Instant attack is deliberate and load-bearing twice over: a meter that rose
    as slowly as it fell would miss every transient, and a test can assert a
    level after ONE frame rather than waiting for a ramp.

    @param deltaMs  how long since the last update; the point of the exercise
                    is that this may vary and the fall must not.
*/
float fall (float current, float incoming, int deltaMs) noexcept;

/** Where a gain sits on the meter, 0 at the floor and 1 at unity.

    Scaled the way a level is heard rather than by amplitude: linear, a healthy
    mix sits in the bottom fifth of the meter and looks broken.
*/
float proportionForGain (float gain) noexcept;

/** Below this a signal is silence, and the meter reads empty. */
inline constexpr double floorDb = -48.0;

/** Where the meter stops being green. A threshold rather than a gradient:
    what a recording meter has to say is "this is about to clip". */
inline constexpr float hotProportion = 0.8f;

} // namespace dew::meter
