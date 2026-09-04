// =============================================================================
// The blocks that describe a voice, and the material it draws on.
//
// One of three translation units defining lang/ResolverImpl.h, which says why
// the class is named rather than file-local.
//
// channel, voicing, rhythm, harmony and section. What they share is that each is
// NAMED and referred to by name from elsewhere, which is why pass one collects
// their names before any of this runs.
// =============================================================================

#include "lang/Resolver.h"

#include "lang/ResolverImpl.h"

#include <algorithm>

#include "lang/Counterpoint.h"
#include "lang/Numbers.h"
#include "lang/ScanCore.h"

namespace dew::lang
{

void Resolver::resolveChannel (const Block& block)
{
    ChannelSpec channel;
    channel.name = std::string (block.name());
    channel.range = block.nameRange();

    if (channel.name.empty())
    {
        diagnostics.error ("E213", "a channel needs a name", block.keywordRange,
                           "try `channel lead {`");
        return;
    }

    forEachStatement (
        block, BlockKind::channel,
        [&] (const KeySpec& spec, const Statement& statement)
        {
            if (statement.key == "instrument")
            {
                if (! asMemberIndex (statement, spec.kind).has_value())
                    wrongValue (statement, spec.kind);
            }
            else if (statement.key == "mixer")
            {
                // The two failures are told apart on purpose. `mixer 1 range
                // C4..C6` on one line is a SHAPE mistake - statements are
                // newline-terminated - and reporting it as "a mixer track is
                // 1 to 32" sends you looking at the number, which is fine.
                const auto value = asInteger (statement);

                if (! value.has_value())
                    wrongValue (statement, spec.kind);
                else if (*value < 1 || *value > 32)
                    diagnostics.error ("E240", "a mixer track is 1 to 32", statement.range);
                else
                    channel.mixerTrack = *value;
            }
            else if (statement.key == "octave")
            {
                const auto value = asInteger (statement);

                if (! value.has_value() || *value < -4 || *value > 4)
                    diagnostics.error ("E231", "an octave shift is -4 to 4", statement.range);
                else
                    channel.octave = *value;
            }
            else if (statement.key == "range")
            {
                if (const auto range = asPitchRange (statement); range.has_value())
                {
                    channel.lowPitch = range->first;
                    channel.highPitch = range->second;
                }
                else
                {
                    wrongValue (statement, spec.kind);
                }
            }
            else if (statement.key == "velocity")
            {
                readVelocity (channel, statement, spec);
            }
        });

    model.channels.push_back (channel);
}

bool Resolver::readScope (const Statement& statement, std::size_t at, Scope& out)
{
    const auto& members = membersOf (ValueKind::scope);

    for (std::size_t i = 0; i < members.size(); ++i)
        if (statement.values[at].text == members[i])
        {
            out = (Scope) i;
            return true;
        }

    auto& d = diagnostics.error (
        "E251", std::string ("`") + std::string (statement.values[at].text) + "` is not a scope",
        statement.values[at].range);

    std::string list;

    for (const auto& member : members)
        list += (list.empty() ? "" : ", ") + std::string (member);

    d.helps.push_back ("one of: " + list);
    return false;
}

void Resolver::readVelocity (ChannelSpec& channel, const Statement& statement, const KeySpec& spec)
{
    // `72`, `72 +- 6`, or `72 +- 6 per bar`.
    auto values = statement.values;

    if (values.size() == 5)
    {
        if (values[3].text != "per")
        {
            wrongValue (statement, spec.kind);
            return;
        }

        if (! readScope (statement, 4, channel.velocityScope))
            return;

        values.resize (3);
    }

    if (values.size() != 1 && values.size() != 3)
    {
        wrongValue (statement, spec.kind);
        return;
    }

    const auto base = readInteger (values.front().text);

    if (! base.has_value() || *base < 1 || *base > 127)
    {
        auto& d = diagnostics.error ("E214", "a velocity is 1 to 127", statement.range);
        d.notes.push_back ("zero IS a note-off in MIDI, so it can never be a "
                           "note's velocity");
        return;
    }

    channel.velocity = (int) *base;

    if (values.size() == 3)
    {
        if (values[1].kind != TokenKind::plusMinus)
        {
            wrongValue (statement, spec.kind);
            return;
        }

        const auto spread = readInteger (values[2].text);

        if (! spread.has_value() || *spread < 0 || *spread > 63)
        {
            wrongValue (statement, spec.kind);
            return;
        }

        channel.velocityJitter = (int) *spread;
    }
}

void Resolver::resolveVoicing (const Block& block)
{
    VoicingSpec voicing;
    voicing.name = std::string (block.name());
    voicing.range = block.nameRange();

    if (voicing.name.empty())
    {
        diagnostics.error ("E213", "a voicing needs a name", block.keywordRange,
                           "try `voicing warm {`");
        return;
    }

    forEachStatement (
        block, BlockKind::voicing,
        [&] (const KeySpec& spec, const Statement& statement)
        {
            if (statement.key == "size")
            {
                // `4` or `4 voices`.
                if (statement.values.empty() || statement.values.size() > 2
                    || (statement.values.size() == 2 && statement.values[1].text != "voices"
                        && statement.values[1].text != "voice"))
                {
                    wrongValue (statement, spec.kind);
                    return;
                }

                const auto value = readInteger (statement.values.front().text);

                if (! value.has_value() || *value < 1 || *value > 8)
                    diagnostics.error ("E215", "a voicing holds 1 to 8 voices", statement.range);
                else
                    voicing.voices = (int) *value;
            }
            else if (statement.key == "spread")
            {
                if (const auto index = asMemberIndex (statement, spec.kind); index.has_value())
                    voicing.spread = (Spread) *index;
                else
                    wrongValue (statement, spec.kind);
            }
            else if (statement.key == "motion")
            {
                if (const auto index = asMemberIndex (statement, spec.kind); index.has_value())
                    voicing.motion = (Motion) *index;
                else
                    wrongValue (statement, spec.kind);
            }
            else if (statement.key == "register")
            {
                if (const auto range = asPitchRange (statement); range.has_value())
                {
                    voicing.lowPitch = range->first;
                    voicing.highPitch = range->second;
                }
                else
                {
                    wrongValue (statement, spec.kind);
                }
            }
            else if (statement.key == "bass")
            {
                if (const auto index = asMemberIndex (statement, spec.kind); index.has_value())
                    voicing.bass = (BassRule) *index;
                else
                    wrongValue (statement, spec.kind);
            }
            else if (statement.key == "maxLeap")
            {
                const auto value = asInteger (statement);

                if (! value.has_value() || *value < 1 || *value > 24)
                    wrongValue (statement, spec.kind);
                else
                    voicing.maxLeap = *value;
            }
        });

    model.voicings.push_back (voicing);
}

RhythmSpec Resolver::resolveRhythm (const Block& block)
{
    RhythmSpec rhythm;
    rhythm.name = std::string (block.name());
    rhythm.range = block.keywordRange;

    for (const auto& entry : block.rhythm)
    {
        RhythmStep step;
        step.range = entry.range;
        step.isRest = entry.kind == RhythmEntry::Kind::rest;
        step.isTie = entry.kind == RhythmEntry::Kind::tie;

        // Every entry carries a length, rests and ties included, so the
        // grid sees them too - a score whose only triplet is a rest still
        // needs a grid that can place it.
        const auto duration = parseDuration (entry.text);

        if (! duration.has_value())
        {
            diagnostics.error ("E216",
                               std::string ("`") + std::string (entry.text) + "` is not a duration",
                               entry.range);
            continue;
        }

        step.duration = *duration;
        noteDuration (*duration, entry.range);

        if (entry.repeat > 256)
        {
            diagnostics.error ("E241", "a rhythm entry repeats at most 256 times", entry.range);
            continue;
        }

        // Each repeat is its OWN step, so a mute budget or a tie can land on
        // any one of them rather than on "the group".
        for (auto i = 0; i < std::max (1, entry.repeat); ++i)
            rhythm.steps.push_back (step);
    }

    if (rhythm.steps.empty())
        diagnostics.error ("E217", "a rhythm needs at least one duration", block.keywordRange);

    return rhythm;
}

HarmonySpec Resolver::resolveHarmony (const Block& block)
{
    HarmonySpec harmony;
    harmony.name = std::string (block.name());
    harmony.range = block.keywordRange;

    forEachStatement (block, BlockKind::harmony,
                      [&] (const KeySpec& spec, const Statement& statement)
                      {
                          if (const auto key = asKey (statement); key.has_value())
                              harmony.key = *key;
                          else
                              wrongValue (statement, spec.kind);
                      });

    const auto key = harmony.key.value_or (model.song.key);

    for (const auto& entry : block.chords)
    {
        ChordSpec chord;
        chord.range = entry.range;
        chord.symbol = { entry.root, entry.hasInversion ? entry.inversion : 0, entry.of };
        chord.barCheckAfter = entry.barCheckAfter;

        // Resolved HERE so a bad numeral is reported where it was written,
        // rather than at generation time when its range is long gone.
        std::string reason;

        if (! resolveChord (chord.symbol, key, &reason).has_value())
        {
            diagnostics.error ("E218", reason.empty() ? "not a chord" : reason, entry.rootRange);
            continue;
        }

        if (entry.hasWeight)
        {
            if (entry.weight < 1)
            {
                diagnostics.error ("E219", "a weight must be at least 1", entry.range);
                continue;
            }

            chord.hasWeight = true;
            chord.weight = entry.weight;
        }
        else if (! entry.duration.empty())
        {
            readChordLength (chord, entry);
        }

        harmony.chords.push_back (chord);
    }

    if (harmony.chords.empty())
        diagnostics.error ("E220", "a harmony needs at least one chord", block.keywordRange);

    return harmony;
}

void Resolver::readChordLength (ChordSpec& chord, const ChordEntry& entry)
{
    const auto& values = entry.duration;

    if (values.size() == 2 && values[0].kind == TokenKind::number
        && (values[1].text == "bar" || values[1].text == "bars"))
    {
        const auto count = readInteger (values[0].text);

        if (! count.has_value() || *count < 1 || *count > 512)
        {
            diagnostics.error ("E221", "a chord spans 1 to 512 bars", entry.range);
            return;
        }

        chord.bars = (int) *count;
        return;
    }

    if (values.size() == 1 && values[0].kind == TokenKind::ratio)
    {
        const auto duration = parseDuration (values[0].text);

        if (! duration.has_value())
        {
            diagnostics.error ("E216", "not a duration", values[0].range);
            return;
        }

        chord.duration = *duration;
        noteDuration (*duration, values[0].range);
        return;
    }

    diagnostics.error ("E222", "a chord takes `xN`, `N bars` or a note value", entry.range,
                       "not that");
}

void Resolver::resolveSection (const Block& block)
{
    SectionSpec section;
    section.name = std::string (block.name());
    section.range = block.nameRange();

    if (section.name.empty())
    {
        diagnostics.error ("E213", "a section needs a name", block.keywordRange,
                           "try `section verse {`");
        return;
    }

    forEachStatement (
        block, BlockKind::section,
        [&] (const KeySpec& spec, const Statement& statement)
        {
            if (statement.key == "length")
            {
                const auto bars = asBars (statement);

                if (! bars.has_value())
                    wrongValue (statement, spec.kind);
                else if (*bars < 1 || *bars > 512)
                    diagnostics.error ("E223", "a section spans 1 to 512 bars", statement.range);
                else
                    section.bars = *bars;
            }
            else if (statement.key == "harmony")
            {
                const auto name = resolveName (statement, symbols.harmonies, "E224", "harmony");

                if (! name.empty())
                    section.harmony = std::string (name);
                else if (statement.values.size() != 1)
                    wrongValue (statement, spec.kind);
            }
        });

    for (const auto& child : block.children)
    {
        const auto kind = blockKindFor (child.keyword);

        if (kind == BlockKind::harmony)
            section.inlineHarmony = resolveHarmony (child);
        else if (kind == BlockKind::part)
            section.parts.push_back (resolvePart (child));
        else
            diagnostics.error ("E225",
                               std::string ("`") + std::string (child.keyword)
                                   + "` is not part of a section",
                               child.keywordRange);
    }

    if (section.harmony.empty() && ! section.inlineHarmony.has_value())
        diagnostics.error ("E226", "a section needs a harmony", block.keywordRange,
                           "name one, or write `harmony { ... }` here");

    if (section.parts.empty())
        diagnostics.error ("E242", "a section needs a part", block.keywordRange,
                           "nothing would sound");

    model.sections.push_back (section);
}

} // namespace dew::lang
