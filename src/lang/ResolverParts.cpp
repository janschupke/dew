// =============================================================================
// The part block, and the three ways a part can get its notes.
//
// One of three translation units defining lang/ResolverImpl.h, which says why
// the class is named rather than file-local.
//
// resolvePart is the hub: a part is a melody, a counterpoint answering another
// voice, or an imitation of one, and it dispatches to whichever was written. The
// read* helpers beside them are the value shapes only these blocks use.
// =============================================================================

#include "lang/Resolver.h"

#include "lang/ResolverImpl.h"

#include <algorithm>

#include "lang/Counterpoint.h"
#include "lang/Numbers.h"
#include "lang/ScanCore.h"

namespace dew::lang
{

PartSpec Resolver::resolvePart (const Block& block)
{
    PartSpec part;
    part.channel = std::string (block.name());
    part.range = block.nameRange();

    if (part.channel.empty())
        diagnostics.error ("E227", "a part needs a channel", block.keywordRange,
                           "try `part lead {`");
    else if (! contains (symbols.channels, part.channel))
        diagnostics.error ("E228", "no channel called `" + part.channel + "`", block.nameRange(),
                           "not declared");

    auto sawKind = false;

    forEachStatement (
        block, BlockKind::part,
        [&] (const KeySpec& spec, const Statement& statement)
        {
            if (statement.key == "chords")
            {
                // `chords with warm`.
                if (statement.values.size() != 2 || statement.values[0].text != "with")
                {
                    diagnostics.error ("E229", "`chords` takes `with <voicing>`", statement.range);
                    return;
                }

                const auto name = statement.values[1].text;

                if (! contains (symbols.voicings, name))
                {
                    diagnostics.error ("E230", "no voicing called `" + std::string (name) + "`",
                                       statement.values[1].range, "not declared");
                    return;
                }

                part.kind = PartKind::chords;
                part.voicing = std::string (name);
                sawKind = true;
            }
            else if (statement.key == "line")
            {
                if (const auto index = asMemberIndex (statement, spec.kind); index.has_value())
                {
                    part.kind = PartKind::line;
                    part.lineSource = (LineSource) *index;
                    sawKind = true;
                }
                else
                {
                    wrongValue (statement, spec.kind);
                }
            }
            else if (statement.key == "rhythm")
            {
                const auto name = resolveName (statement, symbols.rhythms, "E233", "rhythm");

                if (! name.empty())
                    part.rhythm = std::string (name);
                else if (statement.values.size() != 1)
                    wrongValue (statement, spec.kind);
            }
            else if (statement.key == "octave")
            {
                const auto value = asInteger (statement);

                if (! value.has_value() || *value < -4 || *value > 4)
                    diagnostics.error ("E231", "an octave shift is -4 to 4", statement.range);
                else
                    part.octave = *value;
            }
        });

    for (const auto& child : block.children)
    {
        const auto kind = blockKindFor (child.keyword);

        if (kind == BlockKind::rhythm)
        {
            part.inlineRhythm = resolveRhythm (child);
        }
        else if (kind == BlockKind::melody)
        {
            part.kind = PartKind::melody;
            part.melody = resolveMelody (child, part);
            sawKind = true;
        }
        else if (kind == BlockKind::counterpoint)
        {
            part.kind = PartKind::counterpoint;
            part.counterpoint = resolveCounterpoint (child, part);
            sawKind = true;
        }
        else if (kind == BlockKind::imitate)
        {
            part.kind = PartKind::imitation;
            part.imitation = resolveImitation (child, part);
            sawKind = true;
        }
        else
        {
            diagnostics.error (
                "E225", std::string ("`") + std::string (child.keyword) + "` is not part of a part",
                child.keywordRange);
        }
    }

    if (! sawKind)
        diagnostics.error ("E232",
                           "a part needs `chords`, `line`, a `melody`, a "
                           "`counterpoint` or an `imitate` block",
                           block.keywordRange, "nothing here says what to play");

    return part;
}

void Resolver::readLeap (MelodySpec& melody, const Statement& statement, const KeySpec& spec)
{
    const auto& values = statement.values;

    if (values.size() != 2 && values.size() != 4)
    {
        wrongValue (statement, spec.kind);
        return;
    }

    if (values[0].text != "max")
    {
        wrongValue (statement, spec.kind);
        return;
    }

    const auto widest = readInteger (values[1].text);

    if (! widest.has_value() || *widest < 1 || *widest > 24)
    {
        auto& d = diagnostics.error ("E247", "a leap limit is 1 to 24 semitones", values[1].range);
        d.helps.push_back ("12 is an octave");
        return;
    }

    melody.maxLeap = (int) *widest;

    if (values.size() == 2)
        return;

    if (values[2].text != "resolve")
    {
        wrongValue (statement, spec.kind);
        return;
    }

    if (values[3].text == "step")
        melody.resolveLeaps = true;
    else if (values[3].text == "free")
        melody.resolveLeaps = false;
    else
        wrongValue (statement, spec.kind);
}

void Resolver::readCadence (MelodySpec& melody, const Statement& statement, const KeySpec& spec)
{
    const auto& values = statement.values;

    const auto readDegree = [this, &statement] (const Value& value) -> std::optional<int>
    {
        const auto degree = readInteger (value.text);

        // The tones a chord actually has, counted the way musicians count
        // them. `2` is not a chord tone and saying so is better than
        // rounding it to one.
        if (! degree.has_value() || (*degree != 1 && *degree != 3 && *degree != 5 && *degree != 7))
        {
            auto& d = diagnostics.error ("E241", "a cadence ends on a chord tone", value.range);
            d.helps.push_back ("1 is the root, 3 the third, 5 the fifth, 7 the seventh");
            (void) statement;
            return std::nullopt;
        }

        return degree;
    };

    if (values.size() == 1)
    {
        if (const auto degree = readDegree (values.front()))
        {
            melody.cadence.degrees = { *degree };
            melody.cadence.scope = Scope::song; // never re-drawn: it is set
        }

        return;
    }

    // `choose [ a b c ] per <scope>`, and the brackets are real tokens.
    if (values.size() < 5 || values.front().text != "choose"
        || values[1].kind != TokenKind::bracketOpen)
    {
        wrongValue (statement, spec.kind);
        return;
    }

    std::size_t i = 2;
    std::vector<int> degrees;

    for (; i < values.size() && values[i].kind != TokenKind::bracketClose; ++i)
    {
        if (const auto degree = readDegree (values[i]))
            degrees.push_back (*degree);
        else
            return;
    }

    if (i >= values.size() || values[i].kind != TokenKind::bracketClose)
    {
        diagnostics.error ("E242", "this list is never closed", statement.range,
                           "a `[` needs a `]`");
        return;
    }

    if (degrees.empty())
    {
        diagnostics.error ("E243", "a choice needs something to choose from", statement.range);
        return;
    }

    melody.cadence.degrees = degrees;
    melody.cadence.scope = Scope::instance;

    // `per <scope>` is optional; without it a choice is per instance, which
    // is what makes two verses end differently and one verse end once.
    const auto rest = values.size() - (i + 1);

    if (rest == 0)
        return;

    if (rest != 2 || values[i + 1].text != "per")
    {
        wrongValue (statement, spec.kind);
        return;
    }

    readScope (statement, i + 2, melody.cadence.scope);
}

ImitationSpec Resolver::resolveImitation (const Block& block, PartSpec& part)
{
    ImitationSpec spec;

    if (block.header.size() == 1)
    {
        spec.source = std::string (block.name());
        spec.sourceRange = block.header.front().range;

        if (! contains (symbols.channels, spec.source))
            diagnostics.error ("E248", std::string ("no channel called `") + spec.source + "`",
                               spec.sourceRange);
        else if (spec.source == part.channel)
            diagnostics.error ("E249", "a voice cannot imitate itself", spec.sourceRange,
                               "name a different channel");
    }
    else
    {
        auto& d = diagnostics.error ("E250", "imitation needs a voice to copy", block.keywordRange);
        d.helps.push_back ("write `imitate <channel> { delay 1 bar }`");
    }

    forEachStatement (
        block, BlockKind::imitate,
        [&] (const KeySpec& keySpec, const Statement& statement)
        {
            if (statement.key == "delay")
            {
                const auto bars = asBars (statement);

                if (! bars.has_value() || *bars < 0 || *bars > 64)
                    wrongValue (statement, keySpec.kind);
                else
                    spec.delaySteps = *bars; // in BARS here; steps once the grid is known
            }
            else if (statement.key == "transpose")
            {
                if (statement.values.size() == 1)
                    if (const auto value = readInteger (statement.values.front().text);
                        value.has_value() && *value >= -24 && *value <= 24)
                    {
                        spec.transpose = (int) *value;
                        return;
                    }

                wrongValue (statement, keySpec.kind);
            }
            else if (statement.key == "mode")
            {
                if (const auto index = asMemberIndex (statement, keySpec.kind); index.has_value())
                    spec.mode = (TransposeMode) *index;
                else
                    wrongValue (statement, keySpec.kind);
            }
        });

    return spec;
}

CounterpointSpec Resolver::resolveCounterpoint (const Block& block, PartSpec& part)
{
    CounterpointSpec spec;

    if (block.header.size() == 2 && block.header[0].text == "against")
    {
        spec.against = std::string (block.header[1].text);
        spec.againstRange = block.header[1].range;

        if (! contains (symbols.channels, spec.against))
            diagnostics.error ("E244", std::string ("no channel called `") + spec.against + "`",
                               block.header[1].range);
        else if (spec.against == part.channel)
            diagnostics.error ("E245", "a voice cannot answer itself", block.header[1].range,
                               "name a different channel");
    }
    else
    {
        auto& d = diagnostics.error ("E246", "counterpoint needs a voice to answer",
                                     block.keywordRange);
        d.helps.push_back ("write `counterpoint against <channel> { ... }`");
    }

    forEachStatement (
        block, BlockKind::counterpoint,
        [&] (const KeySpec& keySpec, const Statement& statement)
        {
            if (statement.key == "rhythm")
            {
                const auto name = resolveName (statement, symbols.rhythms, "E233", "rhythm");

                if (! name.empty())
                    spec.rhythm = std::string (name);
                else if (statement.values.size() != 1)
                    wrongValue (statement, keySpec.kind);
            }
            else if (statement.key == "articulation")
            {
                if (const auto index = asMemberIndex (statement, keySpec.kind); index.has_value())
                    spec.articulation = (Articulation) *index;
                else
                    wrongValue (statement, keySpec.kind);
            }
            else if (statement.key == "range")
            {
                if (const auto range = asPitchRange (statement); range.has_value())
                {
                    spec.hasRange = true;
                    spec.lowPitch = range->first;
                    spec.highPitch = range->second;
                }
                else
                {
                    wrongValue (statement, keySpec.kind);
                }
            }
            else if (statement.key == "variance")
            {
                if (statement.values.size() == 1)
                    if (const auto value = readNumber (statement.values.front().text))
                    {
                        spec.variance = (float) std::clamp (*value, 0.0, 1.0);
                        return;
                    }

                wrongValue (statement, keySpec.kind);
            }
            else if (statement.key == "align")
            {
                if (const auto index = asMemberIndex (statement, keySpec.kind); index.has_value())
                    spec.align = (Alignment) *index;
                else
                    wrongValue (statement, keySpec.kind);
            }
            else if (const auto rule = ruleFor (statement.key); rule.has_value())
            {
                readRule (spec.rules[(std::size_t) *rule], statement, keySpec);
            }
        });

    return spec;
}

std::optional<CounterpointRule> Resolver::ruleFor (std::string_view key)
{
    for (auto i = 0; i < numCounterpointRules; ++i)
        if (key == nameOf ((CounterpointRule) i))
            return (CounterpointRule) i;

    return std::nullopt;
}

void Resolver::readRule (RuleSetting& setting, const Statement& statement, const KeySpec& spec)
{
    const auto& values = statement.values;

    if (values.size() == 1 && values.front().text == "forbid")
    {
        setting.strength = RuleStrength::forbid;
        return;
    }

    if (values.size() == 2 && values.front().text == "soft")
    {
        if (const auto weight = readNumber (values[1].text); weight.has_value() && *weight >= 0.0)
        {
            setting.strength = RuleStrength::soft;
            setting.weight = (float) *weight;
            return;
        }
    }

    wrongValue (statement, spec.kind);
}

MelodySpec Resolver::resolveMelody (const Block& block, PartSpec& part)
{
    MelodySpec melody;

    forEachStatement (
        block, BlockKind::melody,
        [&] (const KeySpec& spec, const Statement& statement)
        {
            if (statement.key == "rhythm")
            {
                const auto name = resolveName (statement, symbols.rhythms, "E233", "rhythm");

                if (! name.empty())
                    melody.rhythm = std::string (name);
                else if (statement.values.size() != 1)
                    wrongValue (statement, spec.kind);
            }
            else if (statement.key == "articulation")
            {
                if (const auto index = asMemberIndex (statement, spec.kind); index.has_value())
                    melody.articulation = (Articulation) *index;
                else
                    wrongValue (statement, spec.kind);
            }
            else if (statement.key == "contour")
            {
                if (const auto index = asMemberIndex (statement, spec.kind); index.has_value())
                    melody.contour = (Contour) *index;
                else
                    wrongValue (statement, spec.kind);
            }
            else if (statement.key == "strong")
            {
                if (const auto index = asMemberIndex (statement, spec.kind); index.has_value())
                    melody.strong = (StrongRule) *index;
                else
                    wrongValue (statement, spec.kind);
            }
            else if (statement.key == "variance")
            {
                readVariance (melody, statement, spec);
            }
            else if (statement.key == "mute")
            {
                readMuteBudget (melody, statement, spec);
            }
            else if (statement.key == "cadence")
            {
                readCadence (melody, statement, spec);
            }
            else if (statement.key == "leap")
            {
                readLeap (melody, statement, spec);
            }
            else if (statement.key == "align")
            {
                if (const auto index = asMemberIndex (statement, spec.kind); index.has_value())
                    melody.align = (Alignment) *index;
                else
                    wrongValue (statement, spec.kind);
            }
            else if (statement.key == "range")
            {
                if (const auto range = asPitchRange (statement); range.has_value())
                {
                    melody.hasRange = true;
                    melody.lowPitch = range->first;
                    melody.highPitch = range->second;
                }
                else
                {
                    wrongValue (statement, spec.kind);
                }
            }
        });

    for (const auto& child : block.children)
    {
        if (blockKindFor (child.keyword) == BlockKind::rhythm)
            part.inlineRhythm = resolveRhythm (child);
        else
            diagnostics.error ("E225",
                               std::string ("`") + std::string (child.keyword)
                                   + "` is not part of a melody",
                               child.keywordRange);
    }

    return melody;
}

void Resolver::readVariance (MelodySpec& melody, const Statement& statement, const KeySpec& spec)
{
    if (statement.values.size() != 1)
    {
        wrongValue (statement, spec.kind);
        return;
    }

    const auto value = readNumber (statement.values.front().text);

    if (! value.has_value() || *value < 0.0 || *value > 1.0)
    {
        auto& d = diagnostics.error ("E234", "variance is 0 to 1", statement.range);
        d.notes.push_back ("0 is the same notes every compile; above 0 explores, "
                           "still reproducibly");
        return;
    }

    melody.variance = (float) *value;
}

void Resolver::readMuteBudget (MelodySpec& melody, const Statement& statement, const KeySpec& spec)
{
    // `1 of 4`.
    if (statement.values.size() != 3 || statement.values[1].text != "of")
    {
        wrongValue (statement, spec.kind);
        return;
    }

    const auto count = readInteger (statement.values[0].text);
    const auto window = readInteger (statement.values[2].text);

    if (! count.has_value() || ! window.has_value() || *window < 1 || *count < 0 || *window > 1024)
    {
        wrongValue (statement, spec.kind);
        return;
    }

    if (*count >= *window)
    {
        auto& d = diagnostics.error ("E235", "a mute budget has to leave a note sounding",
                                     statement.range);
        d.notes.push_back ("`" + std::to_string (*count) + " of " + std::to_string (*window)
                           + "` would silence every window");
        return;
    }

    melody.muteCount = (int) *count;
    melody.muteWindow = (int) *window;
}

} // namespace dew::lang
