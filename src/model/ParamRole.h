#pragma once

#include <optional>

#include <juce_core/juce_core.h>

#include "model/EffectType.h"

namespace dew
{

/** What a parameter DOES to the sound.

    The third axis of "what kind of thing is this parameter", beside the curve
    it maps on and the control it is edited with - and the one a person reads
    rather than turns. Every knob in dew used to paint the same accent arc, so
    volume, cutoff and attack were told apart only by their captions; on a 34px
    channel row and a 72px mixer strip there is no caption to read.

    Named for the FUNCTION, not the module, which is the whole point: a
    soundfont channel's `filterOffset` and a filter slot's `cutoff` are the same
    idea and get the same colour wherever either one appears.

    Two boundaries are worth stating, because they are the ones that will be
    argued again:

      - `pitch` is a fixed offset to the note; `modulation` is a spread or a
        movement. `detuneCents` holds one oscillator permanently sharp and is
        pitch; `unisonDetune` spreads voices apart to thicken and is modulation.
      - `sustain` is a LEVEL. It is the one ADSR stage that is an amplitude
        rather than a duration, and grouping it with attack and release would be
        grouping it by the panel it sits on.

    `drive` is deliberately not a role of its own: distortion is timbre, so it
    is `tone`. A hue for it would have had to sit between the recording red and
    the playhead amber, in a warm band already spoken for twice.
*/
enum class ParamRole
{
    generic,    ///< multipurpose - mix, a switch, the tempo. Painted in accent.
    level,      ///< how loud
    stereo,     ///< where in the field
    tone,       ///< spectral shaping
    time,       ///< envelope in time
    space,      ///< ambience and echo
    modulation, ///< what makes it move
    pitch       ///< which note you hear
};

// --- the table ---------------------------------------------------------------

/** The role declared for a property, or nothing.

    A lookup on the IDENTIFIER rather than a field on ParamSpec, and that is a
    decision rather than an accident. Every row in ModuleCatalog.cpp is
    positionally aggregate-initialised, so a trailing field would make declaring
    a channel's volume spell out six intervening defaults to reach it - which is
    how a table stops being readable.

    It works because no identifier needs two roles: `gain` is a level on an
    oscillator and on a fader, `pan` is stereo on a channel and on a track,
    `transpose` is pitch on a sample and on a soundfont. That is an assumption,
    not a law, so ParamRoleTests asserts it rather than trusting it.

    Returns an optional so the exhaustiveness test can tell a parameter DECLARED
    generic from one nobody classified. roleOf() below is what a painter calls.
*/
std::optional<ParamRole> declaredRoleOf (const juce::Identifier&) noexcept;

/** What a property does, with `generic` for anything undeclared. */
ParamRole roleOf (const juce::Identifier&) noexcept;

/** What a whole effect does, for the card that holds it.

    Filter, EQ and Drive are all `tone` and Reverb and Delay are both `space`,
    because they ARE - the taxonomy is about function, and two devices can have
    the same one. The type icon is what tells those apart, which is what an icon
    is for.
*/
ParamRole roleOfEffect (EffectType) noexcept;

/** Every property the role table declares, for the test that holds the table
    and the catalog together.

    Both directions are worth checking and neither is checkable from outside
    without this: that every catalogued parameter HAS a role, and that the table
    declares nothing the catalog does not have - a row for a property that was
    renamed away is a row that will never fire again and that nothing else would
    report. automatableParameterNames() exists for the same reason.
*/
juce::StringArray declaredRoleNames();

} // namespace dew
