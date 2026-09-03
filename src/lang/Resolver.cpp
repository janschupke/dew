#include "lang/Resolver.h"

#include "lang/Counterpoint.h"

#include <algorithm>

#include "lang/ScanCore.h"

namespace dew::lang
{

namespace
{

bool contains (const std::vector<std::string>& names, std::string_view name)
{
    return std::find (names.begin(), names.end(), name) != names.end();
}

bool contains (const std::vector<std::string_view>& names, std::string_view name)
{
    return std::find (names.begin(), names.end(), name) != names.end();
}

/** Hand-rolled rather than std::from_chars: its floating-point overloads are
    unavailable below macOS 26, and strtod would want a null-terminated copy and
    a locale. Digits are not hard, and this way the two readers agree about what
    a trailing character means - namely that the whole thing is not a number.
*/
std::optional<long long> readInteger (std::string_view text)
{
    if (text.empty())
        return std::nullopt;

    std::size_t i = 0;
    auto negative = false;

    if (text[i] == '+' || text[i] == '-')
        negative = text[i++] == '-';

    if (i + 2 < text.size() && text[i] == '0' && (text[i + 1] == 'x' || text[i + 1] == 'X'))
    {
        i += 2;
        unsigned long long value = 0;

        for (; i < text.size(); ++i)
        {
            const auto c = text[i];
            auto digit = 0;

            if (c >= '0' && c <= '9')      digit = c - '0';
            else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
            else                           return std::nullopt;

            if (value > 0x0FFFFFFFFFFFFFFFULL)
                return std::nullopt;

            value = value * 16 + (unsigned long long) digit;
        }

        return (long long) value;
    }

    if (i >= text.size())
        return std::nullopt;

    long long value = 0;

    for (; i < text.size(); ++i)
    {
        if (! isDigit (text[i]))
            return std::nullopt;

        if (value > 100000000000LL)
            return std::nullopt;

        value = value * 10 + (text[i] - '0');
    }

    return negative ? -value : value;
}

std::optional<double> readNumber (std::string_view text)
{
    if (text.empty())
        return std::nullopt;

    std::size_t i = 0;
    auto negative = false;

    if (text[i] == '+' || text[i] == '-')
        negative = text[i++] == '-';

    double value = 0.0;
    auto sawDigit = false;

    for (; i < text.size() && isDigit (text[i]); ++i)
    {
        value = value * 10.0 + (text[i] - '0');
        sawDigit = true;
    }

    if (i < text.size() && text[i] == '.')
    {
        ++i;
        auto scale = 0.1;

        for (; i < text.size() && isDigit (text[i]); ++i)
        {
            value += (text[i] - '0') * scale;
            scale *= 0.1;
            sawDigit = true;
        }
    }

    if (! sawDigit || i != text.size())
        return std::nullopt;

    return negative ? -value : value;
}

class Resolver
{
public:
    Resolver (std::string_view src, DiagnosticBag& bag, SymbolTable& table)
        : source (src), diagnostics (bag), symbols (table)
    {
    }

    Model run (const Document& document)
    {
        collectNames (document);
        resolveBlocks (document);
        return std::move (model);
    }

private:
    // --- pass one: names ----------------------------------------------------
    //
    // Separate so a reference may appear before its declaration, and so the
    // symbol table exists even when pass two fails - completion in a file that
    // does not yet compile is the only case that matters.
    void collectNames (const Document& document)
    {
        for (const auto& block : document.blocks)
        {
            const auto name = std::string (block.name());

            if (name.empty())
                continue;

            const auto kind = blockKindFor (block.keyword);

            if (kind == BlockKind::channel)      addName (symbols.channels, name, block);
            else if (kind == BlockKind::voicing) addName (symbols.voicings, name, block);
            else if (kind == BlockKind::rhythm)  addName (symbols.rhythms, name, block);
            else if (kind == BlockKind::harmony) addName (symbols.harmonies, name, block);
            else if (kind == BlockKind::section) addName (symbols.sections, name, block);
        }
    }

    void addName (std::vector<std::string>& names, const std::string& name,
                  const Block& block)
    {
        if (contains (names, name))
        {
            // Reported at the SECOND declaration: the first is not the mistake,
            // and pointing there sends you to the wrong line.
            auto& d = diagnostics.error ("E201", "`" + name + "` is declared twice",
                                         block.nameRange(), "declared again here");
            d.notes.push_back ("rename one of them");
            return;
        }

        names.push_back (name);
    }

    // --- pass two -----------------------------------------------------------
    void resolveBlocks (const Document& document)
    {
        auto sawSong = false;

        for (const auto& block : document.blocks)
        {
            const auto kind = blockKindFor (block.keyword);

            if (kind == BlockKind::song)
            {
                if (sawSong)
                    diagnostics.error ("E202", "a score has one `song` block",
                                       block.keywordRange, "declared again here");

                sawSong = true;
                resolveSong (block);
            }
            else if (kind == BlockKind::channel)     resolveChannel (block);
            else if (kind == BlockKind::voicing)     resolveVoicing (block);
            else if (kind == BlockKind::rhythm)      model.rhythms.push_back (resolveRhythm (block));
            else if (kind == BlockKind::harmony)     model.harmonies.push_back (resolveHarmony (block));
            else if (kind == BlockKind::section)     resolveSection (block);
            else if (kind == BlockKind::arrangement) resolveArrangement (block);
        }

        if (! sawSong)
        {
            const auto end = (std::uint32_t) source.size();
            diagnostics.error ("E203", "a score needs a `song` block", { end, end });
        }
    }

    /** Walks a block's statements against the schema, handing each recognised
        one to `apply`. Unknown keys, duplicates and missing required keys are
        reported here so no individual resolver has to remember to.
    */
    template <typename Apply>
    void forEachStatement (const Block& block, BlockKind kind, Apply&& apply)
    {
        std::vector<std::string_view> seen;

        for (const auto& statement : block.statements)
        {
            const auto* spec = keySpecFor (kind, statement.key);

            if (spec == nullptr)
            {
                auto& d = diagnostics.error ("E204",
                                             std::string ("`") + std::string (statement.key)
                                             + "` is not a key of `" + nameOf (kind) + "`",
                                             statement.keyRange, "unknown key");

                if (const auto suggestion = closestKeyTo (kind, statement.key);
                    ! suggestion.empty())
                    d.helps.push_back ("did you mean `" + std::string (suggestion) + "`?");

                continue;
            }

            if (contains (seen, statement.key))
            {
                diagnostics.error ("E205",
                                   std::string ("`") + std::string (statement.key)
                                   + "` is set twice",
                                   statement.keyRange, "set again here");
                continue;
            }

            seen.push_back (statement.key);
            apply (*spec, statement);
        }

        // ONE diagnostic listing every missing key, not one each. They all
        // anchor to the same line - the block's keyword - and the bag reports
        // at most one error per line, so N separate errors would silently
        // become "needs a tempo" and nothing about the other two. Saying them
        // together is both the honest fix and the better message.
        std::vector<std::string_view> missing;

        for (const auto& key : specFor (kind)->keys)
            if (key.required && ! contains (seen, key.name))
                missing.push_back (key.name);

        if (! missing.empty())
        {
            std::string list;

            for (std::size_t i = 0; i < missing.size(); ++i)
            {
                if (i > 0)
                    list += i + 1 == missing.size() ? " and " : ", ";

                list += "`" + std::string (missing[i]) + "`";
            }

            diagnostics.error ("E206",
                               std::string ("`") + nameOf (kind) + "` needs " + list,
                               block.keywordRange, "missing here");
        }
    }

    void wrongValue (const Statement& statement, ValueKind kind)
    {
        auto& d = diagnostics.error ("E207",
                                     std::string ("`") + std::string (statement.key)
                                     + "` takes " + nameOf (kind),
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

    // --- value readers ------------------------------------------------------
    std::optional<int> asInteger (const Statement& statement)
    {
        if (statement.values.size() != 1)
            return std::nullopt;

        const auto value = readInteger (statement.values.front().text);

        if (! value.has_value() || *value > 1000000 || *value < -1000000)
            return std::nullopt;

        return (int) *value;
    }

    std::optional<std::pair<int, int>> asPitchRange (const Statement& statement)
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

    std::optional<Key> asKey (const Statement& statement)
    {
        if (statement.values.size() != 2)
            return std::nullopt;

        return parseKey (statement.values[0].text, statement.values[1].text);
    }

    /** An enum-shaped value: its index in the schema's member list, which is
        also the enum's own value. One table, two uses.
    */
    std::optional<int> asMemberIndex (const Statement& statement, ValueKind kind)
    {
        if (statement.values.size() != 1)
            return std::nullopt;

        const auto& members = membersOf (kind);
        const auto at = std::find (members.begin(), members.end(),
                                   statement.values.front().text);

        if (at == members.end())
            return std::nullopt;

        return (int) std::distance (members.begin(), at);
    }

    /** `N bars` or `N bar`. */
    std::optional<int> asBars (const Statement& statement)
    {
        if (statement.values.size() != 2
            || (statement.values[1].text != "bar" && statement.values[1].text != "bars"))
            return std::nullopt;

        const auto count = readInteger (statement.values[0].text);

        if (! count.has_value())
            return std::nullopt;

        return (int) *count;
    }

    void noteDuration (Duration duration, SourceRange range)
    {
        model.durationUses.push_back ({ duration, range });
    }

    std::string_view resolveName (const Statement& statement,
                                  const std::vector<std::string>& declared,
                                  const char* code, const char* what)
    {
        if (statement.values.size() != 1)
            return {};

        const auto name = statement.values.front().text;

        if (! contains (declared, name))
        {
            diagnostics.error (code,
                               std::string ("no ") + what + " called `"
                               + std::string (name) + "`",
                               statement.values.front().range, "not declared");
            return {};
        }

        return name;
    }

    // --- song ---------------------------------------------------------------
    void resolveSong (const Block& block)
    {
        forEachStatement (block, BlockKind::song, [&] (const KeySpec& spec,
                                                       const Statement& statement)
        {
            if (statement.key == "title")
            {
                if (statement.values.size() == 1
                    && statement.values.front().kind == TokenKind::text)
                {
                    const auto quoted = statement.values.front().text;
                    model.song.title = std::string (quoted.substr (1, quoted.size() - 2));
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

    void readTempo (const Statement& statement, const KeySpec& spec)
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
            auto& d = diagnostics.error ("E209", "a tempo must be 20 to 400",
                                         statement.range);
            d.notes.push_back ("dew stores one tempo for the whole song");
            return;
        }

        model.song.tempoBpm = *value;
    }

    void readMeter (const Statement& statement, const KeySpec& spec)
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
        if (meter->beatUnit != 1 && meter->beatUnit != 2 && meter->beatUnit != 4
            && meter->beatUnit != 8 && meter->beatUnit != 16)
        {
            auto& d = diagnostics.error ("E211", "a beat unit must be 1, 2, 4, 8 or 16",
                                         statement.range);
            d.notes.push_back ("MIDI stores the unit's base-2 logarithm, "
                               "so it has to be a power of two");
            return;
        }

        model.song.meter = *meter;
    }

    void readGrid (const Statement& statement, const KeySpec& spec)
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
            auto& d = diagnostics.error ("E212",
                                         "a grid is `auto` or 1 to 16 steps per beat",
                                         statement.range);
            d.notes.push_back ("dew stores a note's position as a whole number of "
                               "steps, and stepsPerBeat runs to 16");
            return;
        }

        model.song.gridIsAuto = false;
        model.song.declaredGrid = *value;
    }

    // --- channel ------------------------------------------------------------
    void resolveChannel (const Block& block)
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

        forEachStatement (block, BlockKind::channel, [&] (const KeySpec& spec,
                                                          const Statement& statement)
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

    /** Reads a trailing `per <scope>`, and says whether it was there.

        Returns false only when `per` is present and what follows it is not a
        scope, which is the case worth a diagnostic - `per verse` is a typo, not
        a scope nobody has implemented yet.
    */
    bool readScope (const Statement& statement, std::size_t at, Scope& out)
    {
        const auto& members = membersOf (ValueKind::scope);

        for (std::size_t i = 0; i < members.size(); ++i)
            if (statement.values[at].text == members[i])
            {
                out = (Scope) i;
                return true;
            }

        auto& d = diagnostics.error ("E240",
                                     std::string ("`") + std::string (statement.values[at].text)
                                     + "` is not a scope", statement.values[at].range);

        std::string list;

        for (const auto& member : members)
            list += (list.empty() ? "" : ", ") + std::string (member);

        d.helps.push_back ("one of: " + list);
        return false;
    }

    void readVelocity (ChannelSpec& channel, const Statement& statement,
                       const KeySpec& spec)
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

    // --- voicing ------------------------------------------------------------
    void resolveVoicing (const Block& block)
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

        forEachStatement (block, BlockKind::voicing, [&] (const KeySpec& spec,
                                                          const Statement& statement)
        {
            if (statement.key == "size")
            {
                // `4` or `4 voices`.
                if (statement.values.empty() || statement.values.size() > 2
                    || (statement.values.size() == 2
                        && statement.values[1].text != "voices"
                        && statement.values[1].text != "voice"))
                {
                    wrongValue (statement, spec.kind);
                    return;
                }

                const auto value = readInteger (statement.values.front().text);

                if (! value.has_value() || *value < 1 || *value > 8)
                    diagnostics.error ("E215", "a voicing holds 1 to 8 voices",
                                       statement.range);
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

    // --- rhythm -------------------------------------------------------------
    RhythmSpec resolveRhythm (const Block& block)
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
                                   std::string ("`") + std::string (entry.text)
                                   + "` is not a duration",
                                   entry.range);
                continue;
            }

            step.duration = *duration;
            noteDuration (*duration, entry.range);

            if (entry.repeat > 256)
            {
                diagnostics.error ("E241", "a rhythm entry repeats at most 256 times",
                                   entry.range);
                continue;
            }

            // Each repeat is its OWN step, so a mute budget or a tie can land on
            // any one of them rather than on "the group".
            for (auto i = 0; i < std::max (1, entry.repeat); ++i)
                rhythm.steps.push_back (step);
        }

        if (rhythm.steps.empty())
            diagnostics.error ("E217", "a rhythm needs at least one duration",
                               block.keywordRange);

        return rhythm;
    }

    // --- harmony ------------------------------------------------------------
    HarmonySpec resolveHarmony (const Block& block)
    {
        HarmonySpec harmony;
        harmony.name = std::string (block.name());
        harmony.range = block.keywordRange;

        forEachStatement (block, BlockKind::harmony, [&] (const KeySpec& spec,
                                                          const Statement& statement)
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
                diagnostics.error ("E218", reason.empty() ? "not a chord" : reason,
                                   entry.rootRange);
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
            diagnostics.error ("E220", "a harmony needs at least one chord",
                               block.keywordRange);

        return harmony;
    }

    void readChordLength (ChordSpec& chord, const ChordEntry& entry)
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

        diagnostics.error ("E222", "a chord takes `xN`, `N bars` or a note value",
                           entry.range, "not that");
    }

    // --- section ------------------------------------------------------------
    void resolveSection (const Block& block)
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

        forEachStatement (block, BlockKind::section, [&] (const KeySpec& spec,
                                                          const Statement& statement)
        {
            if (statement.key == "length")
            {
                const auto bars = asBars (statement);

                if (! bars.has_value())
                    wrongValue (statement, spec.kind);
                else if (*bars < 1 || *bars > 512)
                    diagnostics.error ("E223", "a section spans 1 to 512 bars",
                                       statement.range);
                else
                    section.bars = *bars;
            }
            else if (statement.key == "harmony")
            {
                const auto name = resolveName (statement, symbols.harmonies,
                                               "E224", "harmony");

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

    // --- part ---------------------------------------------------------------
    PartSpec resolvePart (const Block& block)
    {
        PartSpec part;
        part.channel = std::string (block.name());
        part.range = block.nameRange();

        if (part.channel.empty())
            diagnostics.error ("E227", "a part needs a channel", block.keywordRange,
                               "try `part lead {`");
        else if (! contains (symbols.channels, part.channel))
            diagnostics.error ("E228", "no channel called `" + part.channel + "`",
                               block.nameRange(), "not declared");

        auto sawKind = false;

        forEachStatement (block, BlockKind::part, [&] (const KeySpec& spec,
                                                       const Statement& statement)
        {
            if (statement.key == "chords")
            {
                // `chords with warm`.
                if (statement.values.size() != 2 || statement.values[0].text != "with")
                {
                    diagnostics.error ("E229", "`chords` takes `with <voicing>`",
                                       statement.range);
                    return;
                }

                const auto name = statement.values[1].text;

                if (! contains (symbols.voicings, name))
                {
                    diagnostics.error ("E230",
                                       "no voicing called `" + std::string (name) + "`",
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
            else
            {
                diagnostics.error ("E225",
                                   std::string ("`") + std::string (child.keyword)
                                   + "` is not part of a part",
                                   child.keywordRange);
            }
        }

        if (! sawKind)
            diagnostics.error ("E232",
                               "a part needs `chords`, `line`, a `melody` or a "
                               "`counterpoint` block",
                               block.keywordRange, "nothing here says what to play");

        return part;
    }

    /** `cadence 1`, or `cadence choose [1 3 5] per instance`.

        A closed shape rather than an expression: a list of chord-tone degrees
        and a scope. The moment a value can be COMPUTED, completion stops being
        a table lookup and the grid stops being statically knowable, and those
        two properties are what the whole language is built on.
    */
    void readCadence (MelodySpec& melody, const Statement& statement, const KeySpec& spec)
    {
        const auto& values = statement.values;

        const auto readDegree = [this, &statement] (const Value& value) -> std::optional<int>
        {
            const auto degree = readInteger (value.text);

            // The tones a chord actually has, counted the way musicians count
            // them. `2` is not a chord tone and saying so is better than
            // rounding it to one.
            if (! degree.has_value() || (*degree != 1 && *degree != 3
                                         && *degree != 5 && *degree != 7))
            {
                auto& d = diagnostics.error ("E241", "a cadence ends on a chord tone",
                                             value.range);
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
                melody.cadence.scope = Scope::song;   // never re-drawn: it is set
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
            diagnostics.error ("E243", "a choice needs something to choose from",
                               statement.range);
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

    // --- counterpoint --------------------------------------------------------
    /** `counterpoint against lead { ... }`.

        The header carries `against <channel>`; the parser keeps every word
        between the keyword and the brace, so nothing about this shape needed a
        parser change.
    */
    CounterpointSpec resolveCounterpoint (const Block& block, PartSpec& part)
    {
        CounterpointSpec spec;

        if (block.header.size() == 2 && block.header[0].text == "against")
        {
            spec.against = std::string (block.header[1].text);
            spec.againstRange = block.header[1].range;

            if (! contains (symbols.channels, spec.against))
                diagnostics.error ("E244",
                                   std::string ("no channel called `") + spec.against + "`",
                                   block.header[1].range);
            else if (spec.against == part.channel)
                diagnostics.error ("E245", "a voice cannot answer itself",
                                   block.header[1].range,
                                   "name a different channel");
        }
        else
        {
            auto& d = diagnostics.error ("E246", "counterpoint needs a voice to answer",
                                         block.keywordRange);
            d.helps.push_back ("write `counterpoint against <channel> { ... }`");
        }

        forEachStatement (block, BlockKind::counterpoint, [&] (const KeySpec& keySpec,
                                                               const Statement& statement)
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
            else if (const auto rule = ruleFor (statement.key); rule.has_value())
            {
                readRule (spec.rules[(std::size_t) *rule], statement, keySpec);
            }
        });

        return spec;
    }

    static std::optional<CounterpointRule> ruleFor (std::string_view key)
    {
        for (auto i = 0; i < numCounterpointRules; ++i)
            if (key == nameOf ((CounterpointRule) i))
                return (CounterpointRule) i;

        return std::nullopt;
    }

    /** `forbid`, or `soft <weight>`. */
    void readRule (RuleSetting& setting, const Statement& statement, const KeySpec& spec)
    {
        const auto& values = statement.values;

        if (values.size() == 1 && values.front().text == "forbid")
        {
            setting.strength = RuleStrength::forbid;
            return;
        }

        if (values.size() == 2 && values.front().text == "soft")
        {
            if (const auto weight = readNumber (values[1].text);
                weight.has_value() && *weight >= 0.0)
            {
                setting.strength = RuleStrength::soft;
                setting.weight = (float) *weight;
                return;
            }
        }

        wrongValue (statement, spec.kind);
    }

    // --- melody -------------------------------------------------------------
    MelodySpec resolveMelody (const Block& block, PartSpec& part)
    {
        MelodySpec melody;

        forEachStatement (block, BlockKind::melody, [&] (const KeySpec& spec,
                                                         const Statement& statement)
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

    void readVariance (MelodySpec& melody, const Statement& statement, const KeySpec& spec)
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

    void readMuteBudget (MelodySpec& melody, const Statement& statement, const KeySpec& spec)
    {
        // `1 of 4`.
        if (statement.values.size() != 3 || statement.values[1].text != "of")
        {
            wrongValue (statement, spec.kind);
            return;
        }

        const auto count = readInteger (statement.values[0].text);
        const auto window = readInteger (statement.values[2].text);

        if (! count.has_value() || ! window.has_value() || *window < 1 || *count < 0
            || *window > 1024)
        {
            wrongValue (statement, spec.kind);
            return;
        }

        if (*count >= *window)
        {
            auto& d = diagnostics.error ("E235",
                                         "a mute budget has to leave a note sounding",
                                         statement.range);
            d.notes.push_back ("`" + std::to_string (*count) + " of "
                               + std::to_string (*window)
                               + "` would silence every window");
            return;
        }

        melody.muteCount = (int) *count;
        melody.muteWindow = (int) *window;
    }

    // --- arrangement --------------------------------------------------------
    void resolveArrangement (const Block& block)
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
                auto& d = diagnostics.error ("E236",
                                             "no section called `" + item.section + "`",
                                             entry.sectionRange, "not declared");

                std::vector<std::string_view> names { symbols.sections.begin(),
                                                      symbols.sections.end() };

                if (const auto suggestion = closestOfNames (names, entry.section);
                    ! suggestion.empty())
                    d.helps.push_back ("did you mean `" + std::string (suggestion) + "`?");

                continue;
            }

            if (entry.repeat > 256)
            {
                diagnostics.error ("E237", "a section repeats at most 256 times",
                                   entry.range);
                continue;
            }

            for (const auto& overrideBlock : entry.overrides)
                for (const auto& child : overrideBlock.children)
                {
                    if (blockKindFor (child.keyword) == BlockKind::part)
                        item.overrides.push_back (resolvePart (child));
                    else
                        diagnostics.error ("E238",
                                           "an instance can only override a `part`",
                                           child.keywordRange);
                }

            model.arrangement.push_back (item);
        }

        if (model.arrangement.empty())
            diagnostics.error ("E239", "the arrangement is empty", block.keywordRange,
                               "nothing would play");
    }

    static std::string_view closestOfNames (const std::vector<std::string_view>& names,
                                            std::string_view text)
    {
        for (const auto& name : names)
            if (name.size() > 2 && text.size() > 2
                && name.substr (0, 2) == text.substr (0, 2))
                return name;

        return {};
    }

    std::string_view source;
    DiagnosticBag& diagnostics;
    SymbolTable& symbols;
    Model model;
};

} // namespace

// ------------------------------------------------------------------------------

const ChannelSpec* Model::channel (std::string_view name) const noexcept
{
    for (const auto& item : channels)
        if (item.name == name)
            return &item;

    return nullptr;
}

const VoicingSpec* Model::voicing (std::string_view name) const noexcept
{
    for (const auto& item : voicings)
        if (item.name == name)
            return &item;

    return nullptr;
}

const RhythmSpec* Model::rhythm (std::string_view name) const noexcept
{
    for (const auto& item : rhythms)
        if (item.name == name)
            return &item;

    return nullptr;
}

const HarmonySpec* Model::harmony (std::string_view name) const noexcept
{
    for (const auto& item : harmonies)
        if (item.name == name)
            return &item;

    return nullptr;
}

const SectionSpec* Model::section (std::string_view name) const noexcept
{
    for (const auto& item : sections)
        if (item.name == name)
            return &item;

    return nullptr;
}

Model resolve (const Document& document, std::string_view source,
               DiagnosticBag& diagnostics, SymbolTable& symbols)
{
    Resolver resolver { source, diagnostics, symbols };
    return resolver.run (document);
}

} // namespace dew::lang
