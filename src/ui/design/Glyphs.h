#pragma once

#include <optional>

#include "model/EffectType.h"
#include "model/InstrumentType.h"
#include "ui/design/Icons.h"
#include "ui/design/Tokens.h"

/** Where a CONCEPT becomes a glyph.

    The join between the two libraries, and the only place it happens, exactly
    as ParamPalette.h is for colour: dew_model declares what kinds of thing
    exist, dew_design declares what shapes there are, and this says which is
    which. Before it there was one such mapping in the whole tree - a private
    static function inside EffectCard.cpp - so nothing stopped the same effect
    wearing one icon on a card and no icon in the picker that adds it.

    Its own header rather than a function in Icons.h, because Icons.h is the
    vocabulary and knows nothing but JUCE. An icon set that had to include the
    document model to name a shape would have stopped being one.
*/
namespace dew::glyph
{

/** A switch with no default, so adding an EffectType is a compile error here
    rather than an effect that silently wears the filter's icon. That is the
    trade EffectCard.cpp already made for the card; this is the same one, made
    once for everything that draws an effect.
*/
inline juce::Path forEffect (EffectType type) noexcept
{
    switch (type)
    {
        case EffectType::filter: return icons::effectFilter();
        case EffectType::reverb: return icons::effectReverb();
        case EffectType::delay: return icons::effectDelay();
        case EffectType::drive: return icons::effectDrive();
        case EffectType::distortion: return icons::effectDistortion();
        case EffectType::chorus: return icons::effectChorus();
        case EffectType::phaser: return icons::effectPhaser();
        case EffectType::eq: return icons::effectEq();
        case EffectType::compressor: return icons::effectCompressor();
        case EffectType::limiter: return icons::effectLimiter();
    }

    jassertfalse;
    return {};
}

inline juce::Path forInstrument (InstrumentType type) noexcept
{
    switch (type)
    {
        case InstrumentType::synth: return icons::instrumentSynth();
        case InstrumentType::audio: return icons::instrumentAudio();
        case InstrumentType::soundfont: return icons::instrumentSoundFont();
    }

    jassertfalse;
    return {};
}

inline juce::Path forWaveform (Waveform wave) noexcept
{
    switch (wave)
    {
        case Waveform::sine: return icons::waveSine();
        case Waveform::saw: return icons::waveSaw();
        case Waveform::square: return icons::waveSquare();
        case Waveform::triangle: return icons::waveTriangle();
    }

    jassertfalse;
    return {};
}

/** What a context menu OFFERS, as a vocabulary rather than as a shape picked at
    each call site.

    Rename, add and remove are written out three times in dew - the channel
    rack's row, the playlist's track header and the mixer's strip - and nothing
    held those three copies together. Naming the action rather than the icon is
    what stops a fourth menu renaming a thing with a different picture.
*/
enum class Action
{
    rename,
    add,
    remove,
    duplicate,
    open,
    reset,
    colour,
    automate,
    preset,

    /** Reorder, as a menu row. The effect card carried these as two more
        buttons in a 34px header, beside a collapse caret drawn with the same
        two glyphs - so the header showed chevron-down twice, meaning "fold
        this" in one place and "move it later" in the other. A chevron is a
        direction; in a menu it has a WORD beside it saying which. */
    moveUp,
    moveDown
};

inline juce::Path forAction (Action action) noexcept
{
    switch (action)
    {
        case Action::rename: return icons::pencil();
        case Action::add: return icons::plus();
        case Action::remove: return icons::trash();
        case Action::duplicate: return icons::duplicate();
        case Action::open: return icons::open();
        case Action::reset: return icons::reset();
        case Action::colour: return icons::palette();
        case Action::automate: return icons::automation();
        case Action::preset: return icons::preset();
        case Action::moveUp: return icons::chevronUp();
        case Action::moveDown: return icons::chevronDown();
    }

    jassertfalse;
    return {};
}

/** What an action MEANS, as a colour, for the actions that mean something.

    The companion to forAction, and here for the same reason: a menu row's
    trash can was drawn in the row's text colour, so every destructive item in
    the application was white while every destructive BUTTON was already red
    through DewIconButton::Role::danger. The two said different things about the
    same act.

    An optional rather than a colour per action, because most actions mean
    nothing in particular and a vocabulary where everything is coloured is one
    where nothing is - the same rule DewIconButton::Role states for buttons.

    Returning it from HERE rather than passing a colour at each call site is
    what stops the next menu adding an uncoloured remove: the tint travels with
    the action, and a call site that names the action has already asked for it.
*/
inline std::optional<juce::Colour> tintFor (Action action) noexcept
{
    switch (action)
    {
        case Action::remove: return tokens::colour::danger;

        case Action::rename:
        case Action::add:
        case Action::duplicate:
        case Action::open:
        case Action::reset:
        case Action::colour:
        case Action::automate:
        case Action::preset:
        case Action::moveUp:
        case Action::moveDown: break;
    }

    return {};
}

/** Every action, in declaration order, for the tests and for the one page that
    has to show them all. A gallery that listed eight of nine would be a design
    system quietly hiding one.
*/
inline constexpr Action allActions[] {
    Action::rename, Action::add,    Action::remove,   Action::duplicate,
    Action::open,   Action::reset,  Action::colour,   Action::automate,
    Action::preset, Action::moveUp, Action::moveDown,
};

} // namespace dew::glyph
