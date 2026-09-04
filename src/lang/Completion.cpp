#include "lang/Completion.h"

#include <algorithm>

#include "lang/Lexer.h"
#include "lang/Model.h"
#include "lang/Music.h"
#include "lang/Parser.h"
#include "lang/Resolver.h"

namespace dew::lang
{

namespace
{

void add (std::vector<Completion>& out, std::string text, std::string detail, CompletionKind kind)
{
    out.push_back ({ std::move (text), std::move (detail), kind });
}

void addAll (std::vector<Completion>& out, const std::vector<std::string>& names, Msg detail,
             Locale locale)
{
    for (const auto& name : names)
        add (out, name, msg (detail, locale), CompletionKind::name);
}

/** The chords worth offering first, in the mode the caret is in.

    This is the completion that makes the language feel like it knows what you
    are doing, and it is a lookup table per mode rather than anything clever.
    Ordered by how often they are actually written, because a list sorted
    alphabetically would put `bIII` above `i`.

    Every entry has to COMPILE. The first draft offered `vii°` and `ii°` with
    the typographic degree sign, which the language does not accept - completion
    that offers what the compiler rejects is worse than none, and a test now
    resolves every one of these against a key.
*/
const std::vector<std::string_view>& numeralsFor (Mode mode)
{
    static const std::vector<std::string_view> major { "I",      "V",      "vi",   "IV",    "ii",
                                                       "iii",    "V7",     "I^1",  "V^1",   "ii7",
                                                       "IVmaj7", "viidim", "V7/V", "V7/vi", "V7/ii",
                                                       "V7/IV",  "bVII",   "iv" };

    static const std::vector<std::string_view> minor { "i",       "iv",    "v",     "bVI",
                                                       "bVII",    "bIII",  "V7",    "i^1",
                                                       "iv^1",    "iidim", "V7/iv", "V7/bVI",
                                                       "V7/bVII", "bII",   "V" };

    switch (mode)
    {
        case Mode::major:
        case Mode::lydian:
        case Mode::mixolydian:
        case Mode::majorPentatonic: return major;

        case Mode::minor:
        case Mode::dorian:
        case Mode::phrygian:
        case Mode::locrian:
        case Mode::harmonicMinor:
        case Mode::melodicMinor:
        case Mode::minorPentatonic:
        case Mode::blues:
        case Mode::chromatic: break;
    }

    return minor;
}

/** How a numeral reads in this key: `bVI` is `F` in A minor.

    The difference between a list of symbols and a list you can choose from. It
    goes through the same resolveChord the compiler uses, so what is shown
    cannot disagree with the notes that get written.

    The numeral has to be taken apart first - `^` carries the inversion and `/`
    the tonicisation, and neither is part of the root - or the offered `i^1` and
    `V7/iv` are the two entries with nothing beside them.
*/
std::string spell (std::string_view numeral, const Key& key)
{
    ChordSymbol symbol;
    symbol.root = numeral;

    if (const auto slash = symbol.root.find ('/'); slash != std::string_view::npos)
    {
        symbol.of = symbol.root.substr (slash + 1);
        symbol.root = symbol.root.substr (0, slash);
    }

    if (const auto caret = symbol.root.find ('^'); caret != std::string_view::npos)
    {
        const auto digits = symbol.root.substr (caret + 1);

        if (digits.size() == 1 && digits[0] >= '0' && digits[0] <= '9')
            symbol.inversion = digits[0] - '0';

        symbol.root = symbol.root.substr (0, caret);
    }

    if (const auto resolved = resolveChord (symbol, key))
        return resolved->chord.label;

    return {};
}

/** Durations, plus the two things that are not notes.

    The text is the language's own spelling and never moves; only the detail
    beside it is a sentence.
*/
void addDurations (std::vector<Completion>& out, Locale locale)
{
    struct Entry
    {
        const char* text;
        Msg detail;
    };

    static const Entry entries[] = {
        { "1/4", Msg::completion_quarter_detail },
        { "1/8", Msg::completion_eighth_detail },
        { "1/2", Msg::completion_half_detail },
        { "1/1", Msg::completion_whole_detail },
        { "1/16", Msg::completion_sixteenth_detail },
        { "1/4.", Msg::completion_dottedQuarter_detail },
        { "1/8.", Msg::completion_dottedEighth_detail },
        { "1/8t", Msg::completion_eighthTriplet_detail },
        { "1/16t", Msg::completion_sixteenthTriplet_detail },
        { "1/32", Msg::completion_thirtySecond_detail },
        { "-", Msg::completion_rest_detail },
        { "~", Msg::completion_tie_detail },
        { "x2", Msg::completion_repeatEntry_detail },
    };

    for (const auto& entry : entries)
        add (out, entry.text, msg (entry.detail, locale), CompletionKind::value);
}

void addRoots (std::vector<Completion>& out, Locale locale)
{
    for (const auto* root :
         { "C", "C#", "Db", "D", "Eb", "E", "F", "F#", "Gb", "G", "Ab", "A", "Bb", "B" })
        add (out, root, msg (Msg::completion_root_detail, locale), CompletionKind::value);
}

void addModes (std::vector<Completion>& out, Locale locale)
{
    // The mode NAMES are keywords - `key F dorian` is written exactly so - and
    // are never translated. Only the word for what they are is.
    for (const auto* mode : { "major", "minor", "dorian", "phrygian", "lydian", "mixolydian",
                              "locrian", "harmonic-minor", "melodic-minor", "major-pentatonic",
                              "minor-pentatonic", "blues", "chromatic" })
        add (out, mode, msg (Msg::completion_mode_detail, locale), CompletionKind::value);
}

/** Candidates for one value position, from its declared kind and nothing else.

    `wordsAlready` is how many words of the value have been written, which is
    the whole of the two-stage handling `key` needs: `key ` offers roots and
    `key F ` offers modes.
*/
void addForKind (std::vector<Completion>& out, ValueKind kind, int wordsAlready,
                 const SymbolTable& symbols, Locale locale)
{
    if (const auto& members = membersOf (kind); ! members.empty())
    {
        for (const auto& member : members)
            add (out, std::string (member), msg (nameOf (kind), locale), CompletionKind::value);

        return;
    }

    switch (kind)
    {
        case ValueKind::key:
            if (wordsAlready == 0)
                addRoots (out, locale);
            else
                addModes (out, locale);
            break;

        case ValueKind::grid:
            add (out, "auto", msg (Msg::completion_gridAuto_detail, locale), CompletionKind::value);
            for (const auto* n : { "4", "8", "12", "16" })
                add (out, n, msg (Msg::completion_gridSteps_detail, locale), CompletionKind::value);
            break;

        case ValueKind::rhythmRef:
            addAll (out, symbols.rhythms, Msg::completion_aRhythm_detail, locale);
            break;
        case ValueKind::voicingRef:
            addAll (out, symbols.voicings, Msg::completion_aVoicing_detail, locale);
            break;
        case ValueKind::harmonyRef:
            addAll (out, symbols.harmonies, Msg::completion_aHarmony_detail, locale);
            break;
        case ValueKind::channelRef:
            addAll (out, symbols.channels, Msg::completion_aChannel_detail, locale);
            break;

        case ValueKind::meter:
            for (const auto* m : { "4/4", "3/4", "6/8", "5/4", "7/8" })
                add (out, m, msg (Msg::completion_meter_detail, locale), CompletionKind::value);
            break;

        case ValueKind::pitchRange:
            for (const auto* r : { "C3..C5", "C2..C4", "C4..C6", "E1..E3" })
                add (out, r, msg (Msg::completion_pitchRange_detail, locale),
                     CompletionKind::value);
            break;

        case ValueKind::cadence:
            if (wordsAlready == 0)
            {
                add (out, "1", msg (Msg::completion_endOnRoot_detail, locale),
                     CompletionKind::value);
                add (out, "3", msg (Msg::completion_endOnThird_detail, locale),
                     CompletionKind::value);
                add (out, "5", msg (Msg::completion_endOnFifth_detail, locale),
                     CompletionKind::value);
                add (out, "7", msg (Msg::completion_endOnSeventh_detail, locale),
                     CompletionKind::value);
                add (out, "choose [1 3 5] per instance",
                     msg (Msg::completion_chooseCadence_detail, locale), CompletionKind::value);
            }
            else
            {
                // Past `choose [...] `, the only thing left to write is a scope.
                add (out, "per", msg (Msg::completion_perScope_detail, locale),
                     CompletionKind::value);

                for (const auto& member : membersOf (ValueKind::scope))
                    add (out, std::string (member), msg (Msg::completion_aScope_detail, locale),
                         CompletionKind::value);
            }
            break;

        // Everything else is a number the user has to choose, and offering
        // "0" or "1" would be noise pretending to be help.
        case ValueKind::text:
        case ValueKind::integer:
        case ValueKind::number:
        case ValueKind::tempo:
        case ValueKind::seed:
        case ValueKind::bars:
        case ValueKind::jitteredInt:
        case ValueKind::voices:
        case ValueKind::muteBudget:
        case ValueKind::spread:
        case ValueKind::motion:
        case ValueKind::contour:
        case ValueKind::strongRule:
        case ValueKind::articulation:
        case ValueKind::lineSource:
        case ValueKind::instrument:
        case ValueKind::scope: // handled above: membersOf lists them
        case ValueKind::bassRule:
        case ValueKind::alignment:
        case ValueKind::transposeMode: break;

        case ValueKind::leapRule:
            if (wordsAlready == 0)
                add (out, "max", msg (Msg::completion_leapMax_detail, locale),
                     CompletionKind::value);
            else
                add (out, "resolve", msg (Msg::completion_leapResolve_detail, locale),
                     CompletionKind::value);
            break;

        case ValueKind::rule:
            add (out, "forbid", msg (Msg::completion_ruleForbid_detail, locale),
                 CompletionKind::value);
            add (out, "soft", msg (Msg::completion_ruleSoft_detail, locale), CompletionKind::value);
            break;
    }
}

bool startsWithIgnoringCase (std::string_view text, std::string_view prefix)
{
    if (prefix.size() > text.size())
        return false;

    for (std::size_t i = 0; i < prefix.size(); ++i)
    {
        const auto a = text[i] >= 'A' && text[i] <= 'Z' ? (char) (text[i] + 32) : text[i];
        const auto b = prefix[i] >= 'A' && prefix[i] <= 'Z' ? (char) (prefix[i] + 32) : prefix[i];

        if (a != b)
            return false;
    }

    return true;
}

/** True if this offset sits inside a comment or a string.

    Offering `section` in the middle of a sentence is not help, it is
    interference - and the tokeniser already knows the difference.
*/
bool insideTrivia (std::string_view source, std::uint32_t offset)
{
    for (const auto& token : tokenize (source))
        if ((token.kind == TokenKind::comment || token.kind == TokenKind::text)
            && offset > token.range.begin && offset <= token.range.end)
            return true;

    return false;
}

} // namespace

CompletionResult completionsAt (std::string_view source, std::uint32_t byteOffset, Locale locale)
{
    CompletionResult result;

    const auto offset = std::min (byteOffset, (std::uint32_t) source.size());

    if (insideTrivia (source, offset))
        return result;

    const auto tokens = tokenizeWithoutTrivia (source);

    // --- what is being typed --------------------------------------------------
    // A word the caret sits in or at the end of is the prefix to filter by, and
    // the range the editor replaces. Anything else and we are inserting.
    int partial = -1;

    for (int i = 0; i < (int) tokens.size(); ++i)
    {
        const auto& token = tokens[(std::size_t) i];

        if (token.kind == TokenKind::endOfFile)
            break;

        if (offset > token.range.begin && offset <= token.range.end)
        {
            partial = i;
            result.replacing = { token.range.begin, offset };
            break;
        }
    }

    const auto prefix = result.replacing.isEmpty() ? std::string_view {}
                                                   : result.replacing.textIn (source);

    // --- which block, and where in a statement --------------------------------
    const LineIndex index { source };
    const auto caretLine = index.lineAt (offset);

    std::vector<BlockKind> stack;

    // The first word of the statement each brace opened, so `part pad {` pushes
    // `part` rather than `pad`.
    std::string_view firstWordOnLine;
    int lastLineSeen = -1;

    std::string_view keyOnCaretLine;
    auto wordsBeforeCaret = 0;

    for (int i = 0; i < (int) tokens.size(); ++i)
    {
        const auto& token = tokens[(std::size_t) i];

        if (token.kind == TokenKind::endOfFile)
            break;

        // Stop at the word being typed, and at anything past the caret.
        if (i == partial || token.range.begin >= offset)
            break;

        const auto line = index.lineAt (token.range.begin);

        if (line != lastLineSeen)
        {
            lastLineSeen = line;
            firstWordOnLine = token.textIn (source);
        }

        if (line == caretLine)
        {
            if (wordsBeforeCaret == 0)
                keyOnCaretLine = token.textIn (source);

            ++wordsBeforeCaret;
        }

        if (token.kind == TokenKind::braceOpen)
        {
            stack.push_back (blockKindFor (firstWordOnLine));

            // A brace ends the statement it opened, so a key written after it
            // on the same line starts fresh.
            if (line == caretLine)
            {
                wordsBeforeCaret = 0;
                keyOnCaretLine = {};
            }
        }
        else if (token.kind == TokenKind::braceClose)
        {
            if (! stack.empty())
                stack.pop_back();

            if (line == caretLine)
            {
                wordsBeforeCaret = 0;
                keyOnCaretLine = {};
            }
        }
    }

    result.block = stack.empty() ? BlockKind::unknown : stack.back();

    // --- the candidates -------------------------------------------------------
    // Names first, because the file has to be read for them and the read is the
    // same one the compiler does - so a name cannot be offered that the
    // resolver would then reject.
    DiagnosticBag bag { source };
    SymbolTable symbols;
    const auto model = resolve (parse (source, bag), source, bag, symbols);

    std::vector<Completion> items;

    const auto atStatementStart = wordsBeforeCaret == 0;

    // Three blocks hold a LIST of values rather than key/value statements, so
    // position within the line means nothing in them.
    if (result.block == BlockKind::harmony)
    {
        // Chords first and `key` last, because a harmony block is chords: the
        // one key it accepts is written once at the top and never again.
        //
        // Each one is SPELLED beside it - `bVI` reads as `F` in A minor - which
        // is the difference between a list of symbols and a list you can choose
        // from. It comes from the same resolveChord the compiler uses, so the
        // spelling shown cannot disagree with the notes written.
        for (const auto& numeral : numeralsFor (model.song.key.mode))
            add (items, std::string (numeral), spell (numeral, model.song.key),
                 CompletionKind::value);

        if (atStatementStart)
            add (items, "key", msg (Msg::doc_harmonyKey_doc, locale), CompletionKind::key);
    }
    else if (result.block == BlockKind::rhythm)
    {
        addDurations (items, locale);
    }
    else if (result.block == BlockKind::arrangement)
    {
        if (atStatementStart)
        {
            addAll (items, symbols.sections, Msg::completion_aSection_detail, locale);
        }
        else
        {
            add (items, "x2", msg (Msg::completion_repeatSection_detail, locale),
                 CompletionKind::value);
            add (items, "identical", msg (Msg::completion_identicalSection_detail, locale),
                 CompletionKind::value);
            add (items, "as", msg (Msg::completion_labelInstance_detail, locale),
                 CompletionKind::value);
        }
    }
    else if (atStatementStart)
    {
        if (result.block == BlockKind::unknown)
        {
            // The top level, from the table that declares which blocks belong
            // there. A hand-written exclusion list here offered `counterpoint`
            // as a top-level keyword the day one was added.
            for (const auto& spec : schema())
                if (spec.topLevel)
                    add (items, nameOf (spec.kind), msg (spec.doc, locale), CompletionKind::block);
        }
        else if (const auto* spec = specFor (result.block); spec != nullptr)
        {
            for (const auto& key : spec->keys)
                add (items, std::string (key.name), msg (key.doc, locale), CompletionKind::key);

            for (const auto child : spec->children)
                if (const auto* childSpec = specFor (child); childSpec != nullptr)
                    add (items, nameOf (child), msg (childSpec->doc, locale),
                         CompletionKind::block);
        }
    }
    else if (const auto* key = keySpecFor (result.block, keyOnCaretLine); key != nullptr)
    {
        addForKind (items, key->kind, wordsBeforeCaret - 1, symbols, locale);
    }
    else if (blockKindFor (keyOnCaretLine) == BlockKind::part)
    {
        // `part <channel>` - the one block whose name has to be something
        // already declared.
        addAll (items, symbols.channels, Msg::completion_aChannel_detail, locale);
    }

    // --- filter ---------------------------------------------------------------
    for (auto& item : items)
        if (startsWithIgnoringCase (item.text, prefix))
            result.items.push_back (std::move (item));

    return result;
}

} // namespace dew::lang
