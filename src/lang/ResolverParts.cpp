// =============================================================================
// The part block, imitation and counterpoint.
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

} // namespace dew::lang
