// =============================================================================
// The resolver's two passes, its value readers, and the song and arrangement blocks.
//
// One of three translation units defining lang/ResolverImpl.h, which says why
// the class is named rather than file-local.
//
// The entry point, the walk that dispatches on block kind, the readers that turn
// one statement into one typed value, and the two blocks describing the piece as
// a whole rather than a voice in it.
// =============================================================================

#include "lang/Resolver.h"

#include "lang/ResolverImpl.h"

#include <algorithm>

#include "lang/Counterpoint.h"
#include "lang/Numbers.h"
#include "lang/ScanCore.h"

namespace dew::lang
{

Model Resolver::run (const Document& document)
{
    collectNames (document);
    resolveBlocks (document);
    return std::move (model);
}

void Resolver::collectNames (const Document& document)
{
    for (const auto& block : document.blocks)
    {
        const auto name = std::string (block.name());

        if (name.empty())
            continue;

        const auto kind = blockKindFor (block.keyword);

        if (kind == BlockKind::channel)
            addName (symbols.channels, name, block);
        else if (kind == BlockKind::voicing)
            addName (symbols.voicings, name, block);
        else if (kind == BlockKind::rhythm)
            addName (symbols.rhythms, name, block);
        else if (kind == BlockKind::harmony)
            addName (symbols.harmonies, name, block);
        else if (kind == BlockKind::section)
            addName (symbols.sections, name, block);
    }
}

void Resolver::addName (std::vector<std::string>& names, const std::string& name,
                        const Block& block)
{
    if (contains (names, name))
    {
        // Reported at the SECOND declaration: the first is not the mistake,
        // and pointing there sends you to the wrong line.
        auto& d = diagnostics.error ("E201", "`" + name + "` is declared twice", block.nameRange(),
                                     "declared again here");
        d.notes.push_back ("rename one of them");
        return;
    }

    names.push_back (name);
}

void Resolver::resolveBlocks (const Document& document)
{
    auto sawSong = false;

    for (const auto& block : document.blocks)
    {
        const auto kind = blockKindFor (block.keyword);

        if (kind == BlockKind::song)
        {
            if (sawSong)
                diagnostics.error ("E202", "a score has one `song` block", block.keywordRange,
                                   "declared again here");

            sawSong = true;
            resolveSong (block);
        }
        else if (kind == BlockKind::channel)
            resolveChannel (block);
        else if (kind == BlockKind::voicing)
            resolveVoicing (block);
        else if (kind == BlockKind::rhythm)
            model.rhythms.push_back (resolveRhythm (block));
        else if (kind == BlockKind::harmony)
            model.harmonies.push_back (resolveHarmony (block));
        else if (kind == BlockKind::section)
            resolveSection (block);
        else if (kind == BlockKind::arrangement)
            resolveArrangement (block);
    }

    if (! sawSong)
    {
        const auto end = (std::uint32_t) source.size();
        diagnostics.error ("E203", "a score needs a `song` block", { end, end });
    }
}

void Resolver::wrongValue (const Statement& statement, ValueKind kind)
{
    auto& d = diagnostics.error (
        "E207", std::string ("`") + std::string (statement.key) + "` takes " + nameOf (kind),
        statement.range, "not that");

    if (const auto& members = membersOf (kind); ! members.empty())
    {
        std::string list;

        for (const auto& member : members)
            list += (list.empty() ? "" : ", ") + std::string (member);

        d.notes.push_back ("one of: " + list);

        if (statement.values.size() == 1)
            if (const auto suggestion = closestMemberTo (kind, statement.values.front().text);
                ! suggestion.empty())
                d.helps.push_back ("did you mean `" + std::string (suggestion) + "`?");
    }
}

std::optional<int> Resolver::asInteger (const Statement& statement)
{
    if (statement.values.size() != 1)
        return std::nullopt;

    const auto value = readInteger (statement.values.front().text);

    if (! value.has_value() || *value > 1000000 || *value < -1000000)
        return std::nullopt;

    return (int) *value;
}

std::optional<std::pair<int, int>> Resolver::asPitchRange (const Statement& statement)
{
    if (statement.values.size() != 3 || statement.values[1].kind != TokenKind::range)
        return std::nullopt;

    const auto low = parsePitch (statement.values[0].text);
    const auto high = parsePitch (statement.values[2].text);

    if (! low.has_value() || ! high.has_value())
        return std::nullopt;

    if (*high < *low)
    {
        diagnostics.error ("E208", "this range runs backwards", statement.range,
                           std::string (statement.values[0].text) + " is above "
                               + std::string (statement.values[2].text));
        return std::nullopt;
    }

    return std::pair { *low, *high };
}

std::optional<Key> Resolver::asKey (const Statement& statement)
{
    if (statement.values.size() != 2)
        return std::nullopt;

    return parseKey (statement.values[0].text, statement.values[1].text);
}

std::optional<int> Resolver::asMemberIndex (const Statement& statement, ValueKind kind)
{
    if (statement.values.size() != 1)
        return std::nullopt;

    const auto& members = membersOf (kind);
    const auto at = std::find (members.begin(), members.end(), statement.values.front().text);

    if (at == members.end())
        return std::nullopt;

    return (int) std::distance (members.begin(), at);
}

std::optional<int> Resolver::asBars (const Statement& statement)
{
    if (statement.values.size() != 2
        || (statement.values[1].text != "bar" && statement.values[1].text != "bars"))
        return std::nullopt;

    const auto count = readInteger (statement.values[0].text);

    if (! count.has_value())
        return std::nullopt;

    return (int) *count;
}

void Resolver::noteDuration (Duration duration, SourceRange range)
{
    model.durationUses.push_back ({ duration, range });
}

std::string_view Resolver::resolveName (const Statement& statement,
                                        const std::vector<std::string>& declared, const char* code,
                                        const char* what)
{
    if (statement.values.size() != 1)
        return {};

    const auto name = statement.values.front().text;

    if (! contains (declared, name))
    {
        diagnostics.error (code,
                           std::string ("no ") + what + " called `" + std::string (name) + "`",
                           statement.values.front().range, "not declared");
        return {};
    }

    return name;
}

void Resolver::resolveSong (const Block& block)
{
    forEachStatement (block, BlockKind::song,
                      [&] (const KeySpec& spec, const Statement& statement)
                      {
                          if (statement.key == "title")
                          {
                              if (statement.values.size() == 1
                                  && statement.values.front().kind == TokenKind::text)
                              {
                                  const auto quoted = statement.values.front().text;
                                  model.song.title = std::string (
                                      quoted.substr (1, quoted.size() - 2));
                              }
                              else
                              {
                                  wrongValue (statement, spec.kind);
                              }
                          }
                          else if (statement.key == "tempo")
                          {
                              readTempo (statement, spec);
                          }
                          else if (statement.key == "meter")
                          {
                              readMeter (statement, spec);
                          }
                          else if (statement.key == "grid")
                          {
                              readGrid (statement, spec);
                          }
                          else if (statement.key == "key")
                          {
                              if (const auto key = asKey (statement); key.has_value())
                                  model.song.key = *key;
                              else
                                  wrongValue (statement, spec.kind);
                          }
                          else if (statement.key == "seed")
                          {
                              if (statement.values.size() != 1)
                              {
                                  wrongValue (statement, spec.kind);
                                  return;
                              }

                              if (const auto value = readInteger (statement.values.front().text);
                                  value.has_value())
                                  model.song.seed = (std::uint64_t) *value;
                              else
                                  wrongValue (statement, spec.kind);
                          }
                      });
}

void Resolver::readTempo (const Statement& statement, const KeySpec& spec)
{
    // `96` or `96 bpm` - the unit is optional and reads well either way.
    if (statement.values.empty() || statement.values.size() > 2
        || (statement.values.size() == 2 && statement.values[1].text != "bpm"))
    {
        wrongValue (statement, spec.kind);
        return;
    }

    const auto value = readNumber (statement.values.front().text);

    if (! value.has_value() || *value < 20.0 || *value > 400.0)
    {
        auto& d = diagnostics.error ("E209", "a tempo must be 20 to 400", statement.range);
        d.notes.push_back ("dew stores one tempo for the whole song");
        return;
    }

    model.song.tempoBpm = *value;
}

void Resolver::readMeter (const Statement& statement, const KeySpec& spec)
{
    if (statement.values.size() != 1)
    {
        wrongValue (statement, spec.kind);
        return;
    }

    const auto meter = parseTimeSignature (statement.values.front().text);

    if (! meter.has_value())
    {
        wrongValue (statement, spec.kind);
        return;
    }

    if (meter->beatsPerBar < 1 || meter->beatsPerBar > 16)
    {
        diagnostics.error ("E210", "a bar holds 1 to 16 beats", statement.range);
        return;
    }

    // dew's Meter requires a power-of-two beat unit because MIDI stores the
    // unit's base-2 logarithm. Refusing here beats being clamped later.
    if (meter->beatUnit != 1 && meter->beatUnit != 2 && meter->beatUnit != 4 && meter->beatUnit != 8
        && meter->beatUnit != 16)
    {
        auto& d = diagnostics.error ("E211", "a beat unit must be 1, 2, 4, 8 or 16",
                                     statement.range);
        d.notes.push_back ("MIDI stores the unit's base-2 logarithm, "
                           "so it has to be a power of two");
        return;
    }

    model.song.meter = *meter;
}

void Resolver::readGrid (const Statement& statement, const KeySpec& spec)
{
    if (statement.values.size() != 1)
    {
        wrongValue (statement, spec.kind);
        return;
    }

    model.song.gridRange = statement.range;

    if (statement.values.front().text == "auto")
    {
        model.song.gridIsAuto = true;
        return;
    }

    const auto value = asInteger (statement);

    if (! value.has_value() || *value < 1 || *value > maxStepsPerBeat)
    {
        auto& d = diagnostics.error ("E212", "a grid is `auto` or 1 to 16 steps per beat",
                                     statement.range);
        d.notes.push_back ("dew stores a note's position as a whole number of "
                           "steps, and stepsPerBeat runs to 16");
        return;
    }

    model.song.gridIsAuto = false;
    model.song.declaredGrid = *value;
}

void Resolver::resolveArrangement (const Block& block)
{
    for (const auto& entry : block.arrangement)
    {
        ArrangementItem item;
        item.section = std::string (entry.section);
        item.range = entry.sectionRange;
        item.label = std::string (entry.label);
        item.repeat = std::max (1, entry.repeat);
        item.identical = entry.identical;

        if (! contains (symbols.sections, item.section))
        {
            // At the REFERENCE, not at any definition: the reference is the
            // mistake, and pointing elsewhere sends you to the wrong line.
            auto& d = diagnostics.error ("E236", "no section called `" + item.section + "`",
                                         entry.sectionRange, "not declared");

            std::vector<std::string_view> names { symbols.sections.begin(),
                                                  symbols.sections.end() };

            if (const auto suggestion = closestOfNames (names, entry.section); ! suggestion.empty())
                d.helps.push_back ("did you mean `" + std::string (suggestion) + "`?");

            continue;
        }

        if (entry.repeat > 256)
        {
            diagnostics.error ("E237", "a section repeats at most 256 times", entry.range);
            continue;
        }

        for (const auto& overrideBlock : entry.overrides)
            for (const auto& child : overrideBlock.children)
            {
                if (blockKindFor (child.keyword) == BlockKind::part)
                    item.overrides.push_back (resolvePart (child));
                else
                    diagnostics.error ("E238", "an instance can only override a `part`",
                                       child.keywordRange);
            }

        model.arrangement.push_back (item);
    }

    if (model.arrangement.empty())
        diagnostics.error ("E239", "the arrangement is empty", block.keywordRange,
                           "nothing would play");
}

std::string_view Resolver::closestOfNames (const std::vector<std::string_view>& names,
                                           std::string_view text)
{
    for (const auto& name : names)
        if (name.size() > 2 && text.size() > 2 && name.substr (0, 2) == text.substr (0, 2))
            return name;

    return {};
}

Model resolve (const Document& document, std::string_view source, DiagnosticBag& diagnostics,
               SymbolTable& symbols)
{
    Resolver resolver { source, diagnostics, symbols };
    return resolver.run (document);
}

} // namespace dew::lang
