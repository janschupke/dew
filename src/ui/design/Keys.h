#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "i18n/Strings.h"
#include "ui/design/Gestures.h"

#include <cmath>
#include <vector>

/** What a key IS, and the keys a value control answers.

    The application's key table lives one layer up, in ui/Hotkeys.h, and has to
    stay there: it names CommandIDs::compileScore, addChannel and fileRender,
    and this library's own CMakeLists says of itself "knows nothing about a
    project". What moved down here is the MECHANISM - a stroke, a binding, and
    the one comparison - plus the one table whose controls live in this layer.

    That split is the shape dew already uses twice. dew_i18n defines StringId
    and tr while every layer above declares its own sentences; ParamSpec defines
    the shape while ModuleCatalog and EffectCatalog each declare rows. Keys.h
    says what a stroke is; Hotkeys.cpp declares the application's, and
    valueKeys::table below declares the design system's own.

    ui/Hotkeys.h includes this and aliases hotkeys::Stroke and hotkeys::Binding
    onto it, so nothing that already spells them had to change.
*/
namespace dew::keys
{

namespace detail
{

/** The modifiers a binding is allowed to be picky about.

    Shift is absent, and that is the rule rather than an omission - see
    matches() below.
*/
inline constexpr int comparedModifiers = juce::ModifierKeys::commandModifier
                                         | juce::ModifierKeys::ctrlModifier
                                         | juce::ModifierKeys::altModifier;

inline int lowerCase (int character) noexcept
{
    return (int) juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) character);
}

} // namespace detail

/** One key, spelled once. `keyCode` is a juce::KeyPress code or a character;
    `modifiers` is a juce::ModifierKeys flag set.

    An action reachable two ways - select-all answers to command AND to control
    - gets two rows rather than a second field. Two rows say which two keys
    they are; a field would have to say how the two relate.
*/
struct Stroke
{
    int keyCode = 0;
    int modifiers = 0;
};

template <typename Action> struct Binding
{
    Action action;
    Stroke stroke;

    /** What this command is CALLED, what it does, and which menu it belongs
        to - as catalogue keys rather than sentences, so the menu bar and the
        command manager read the same row in whatever language is running. */
    StringId name;
    StringId description;
    StringId category;
};

/** The stroke as a KeyPress - the ONE place a KeyPress is constructed. */
inline juce::KeyPress keyPressFor (const Stroke& stroke)
{
    return juce::KeyPress (stroke.keyCode, juce::ModifierKeys (stroke.modifiers), 0);
}

/** Whether a key press is this stroke.

    Command, control and alt are compared EXACTLY. That is the whole fix for
    cmd-1: the map this replaced compared a character and nothing else, so
    every modified digit resolved to a tool.

    Shift is deliberately NOT compared. `+` and `_` are how a keyboard spells
    shift-`=` and shift-`-`, and no binding here is distinguished by shift, so
    a map that compared it would refuse the one key it most has to accept. A
    binding that ever needs shift changes this rule and says so in its row -
    valueKeys below is where that finally happens, and it does it the way the
    piano roll's transpose already does: shift is a VARIANT read off the
    KeyPress at the call site, sizing a step rather than selecting a row.
*/
inline bool matches (const Stroke& stroke, const juce::KeyPress& key) noexcept
{
    if ((key.getModifiers().getRawFlags() & detail::comparedModifiers)
        != (stroke.modifiers & detail::comparedModifiers))
        return false;

    // The code first, because a KeyPress built from a code alone carries no
    // text character - which is every KeyPress a test makes, and which is why
    // the map this replaced had to be reachable both ways to be testable at
    // all. Case-insensitively, because a letter pressed with a modifier
    // reports upper case on some layouts and lower on others.
    if (detail::lowerCase (key.getKeyCode()) == detail::lowerCase (stroke.keyCode))
        return true;

    const auto typed = (int) key.getTextCharacter();

    return typed != 0 && detail::lowerCase (typed) == detail::lowerCase (stroke.keyCode);
}

/** The keys that move a VALUE: a knob, a fader, a stepper, a number field.

    These live beside the controls rather than in the application's table for
    the same reason gesture:: does - they are answered by a primitive, and a
    primitive cannot see dew_ui. hotkeys::value() re-exports them so the
    collision test still walks one registry.

    Bare arrows are DELIBERATELY the same stroke as the timelines'
    ViewCommand::cursorUp and friends, and that is not a collision: a canvas is
    never an ancestor of a slider, so a key press reaches exactly one of the two
    handlers. HotkeyTests pins that rather than leaving it to be rediscovered.
*/
namespace valueKeys
{

enum class Command
{
    none,
    increase,
    decrease,
    coarseIncrease,
    coarseDecrease
};

/** How far one press moves, as a fraction of the control's NORMALISED range.

    A hundred presses end to end. juce::Slider used to step by getInterval(),
    which the catalog sets to 0.001 on volume, pan, sustain, release and gain -
    a thousand presses to cross a fader, which is not an editing gesture.
*/
inline constexpr double normalFraction = 0.01;

/** Page up and page down: ten presses end to end.

    Bound at all because juce::Slider::keyPressed handles the four arrows and
    nothing else, so these two keys were free on every control in dew - the one
    place to put a bigger step that costs no modifier and collides with nothing
    on any of the three platforms dew ships.
*/
inline constexpr double coarseFraction = 0.10;

inline const std::vector<Binding<Command>>& table()
{
    // A function-local static rather than a namespace-scope array: the
    // KeyPress constants are static const ints, not constants a static
    // initialiser can read in a defined order.
    static const std::vector<Binding<Command>> rows {
        { Command::increase,
          { juce::KeyPress::upKey, 0 },
          StringId::command_valueIncrease_name,
          StringId::command_valueIncrease_description,
          StringId::command_category_edit },
        { Command::increase,
          { juce::KeyPress::rightKey, 0 },
          StringId::command_valueIncrease_name,
          StringId::command_valueIncrease_description,
          StringId::command_category_edit },
        { Command::decrease,
          { juce::KeyPress::downKey, 0 },
          StringId::command_valueDecrease_name,
          StringId::command_valueDecrease_description,
          StringId::command_category_edit },
        { Command::decrease,
          { juce::KeyPress::leftKey, 0 },
          StringId::command_valueDecrease_name,
          StringId::command_valueDecrease_description,
          StringId::command_category_edit },

        { Command::coarseIncrease,
          { juce::KeyPress::pageUpKey, 0 },
          StringId::command_valueCoarseIncrease_name,
          StringId::command_valueCoarseIncrease_description,
          StringId::command_category_edit },
        { Command::coarseDecrease,
          { juce::KeyPress::pageDownKey, 0 },
          StringId::command_valueCoarseDecrease_name,
          StringId::command_valueCoarseDecrease_description,
          StringId::command_category_edit },
    };

    return rows;
}

inline Command commandFor (const juce::KeyPress& key) noexcept
{
    for (const auto& binding : table())
        if (matches (binding.stroke, key))
            return binding.action;

    return Command::none;
}

/** Which way a command moves the value, or 0 for none. */
inline int directionOf (Command command) noexcept
{
    switch (command)
    {
        case Command::increase:
        case Command::coarseIncrease: return 1;

        case Command::decrease:
        case Command::coarseDecrease: return -1;

        case Command::none: break;
    }

    return 0;
}

/** How far one press moves, given the modifiers it arrived with.

    Fine is ZERO, and that is the interesting part rather than a hole:
    steppedValue guarantees at least one interval, so a zero fraction lands on
    exactly one. That is the right meaning of "finer" on a control whose
    interval is its finest legal value, AND it is right whatever the curve does,
    because an interval is a distance in VALUE while a fraction is a distance in
    normalised position - the two cannot be converted without knowing where on
    the curve you are standing.

    A continuous control has no such floor, so fine there is the ordinary
    fraction scaled by the multiplier a fine DRAG already uses. "Shift means
    finer wherever a gesture changes a value" is then spelled once, in
    Gestures.h, and this is the keyboard reading it.
*/
inline double fractionFor (Command command, const juce::ModifierKeys& mods,
                           double interval) noexcept
{
    if (command == Command::coarseIncrease || command == Command::coarseDecrease)
        return coarseFraction;

    if (! gesture::isFine (mods))
        return normalFraction;

    return interval > 0.0 ? 0.0 : normalFraction * gesture::fineMultiplier;
}

/** `value`, moved one press, in the control's own units.

    Takes the two conversions rather than a control, because the two callers do
    not share a shape: a juce::Slider carries a skewed NormalisableRange and a
    DewNumberField carries a bool and does the logarithm itself. They share a
    DECISION, which is the same split gesture::intentOf makes for the wheel.

    Note the slider's curve and the field's are not the same function. A
    NormalisableRange skew is a POWER curve; the field, like ParamSpec, uses a
    true exponential. So a step is a constant ratio in a field and only roughly
    one on a knob - GestureTests pins that it is an order of magnitude closer to
    constant than a flat step, which is the claim that is actually true. Each
    control is wrong in the same direction as its own DRAG, which is the pair a
    hand can feel; agreeing with the other control is not.

    A template rather than std::function - a key press is not hot, but
    AllocationGateTests exists and this costs nothing.

    The caller clamps: Slider::setValue runs constrainedValue and
    DewNumberField::commit clamps and snaps, so overshooting the top by an
    interval here lands on the top rather than past it.
*/
template <typename ToNormalised, typename FromNormalised>
double steppedValue (double value, double fraction, int direction, double interval,
                     ToNormalised&& toNormalised, FromNormalised&& fromNormalised)
{
    const auto position = juce::jlimit (0.0, 1.0,
                                        toNormalised (value) + fraction * (double) direction);

    auto moved = fromNormalised (position);

    // A discrete or integral parameter has to move by a WHOLE unit or not at
    // all. One per cent of a nought-to-seven stepper is 0.07, and the snap on
    // the way in would put it straight back where it started - an arrow key
    // that does nothing, which is the defect this whole change is about.
    if (interval > 0.0 && std::abs (moved - value) < interval)
        moved = value + interval * (double) direction;

    return moved;
}

} // namespace valueKeys

} // namespace dew::keys
