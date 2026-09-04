#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "lang/Ast.h"
#include "lang/Diagnostics.h"
#include "lang/Model.h"
#include "lang/Numbers.h"
#include "lang/Schema.h"

namespace dew::lang
{

/** The resolver, declared so that it can be defined in more than one file.

    INTERNAL. lang/Resolver.h is this layer's public surface and declares one
    function; nothing outside the three Resolver*.cpp files includes this.

    It was a class in an anonymous namespace, which is what the parser and the
    generator still are. It reached 1,556 lines over eleven kinds of block, and
    internal linkage is exactly what stopped it being split: a file-local class
    cannot be defined in two translation units. So it is named here, and its
    methods are defined across three - the passes and the whole-piece blocks,
    the blocks that describe a voice, and the part block with the three ways a
    part gets its notes.

    Two members stay inline below and have to. The constructor is its member
    list, and forEachStatement is a template: it is the spine every block
    resolver walks its statements with, reporting unknown keys, duplicates and
    missing required keys so that no individual resolver has to remember to.
*/
class Resolver
{
public:
    Resolver (std::string_view src, DiagnosticBag& bag, SymbolTable& table)
        : source (src)
        , diagnostics (bag)
        , symbols (table)
    {
    }

    Model run (const Document& document);

private:
    // --- pass one: names ----------------------------------------------------
    //
    // Separate so a reference may appear before its declaration, and so the
    // symbol table exists even when pass two fails - completion in a file that
    // does not yet compile is the only case that matters.
    void collectNames (const Document& document);

    void addName (std::vector<std::string>& names, const std::string& name, const Block& block);

    // --- pass two -----------------------------------------------------------
    void resolveBlocks (const Document& document);

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
                auto& d = diagnostics.error (
                    "E204",
                    diagnostics.text (
                        Msg::resolver_unknownKey_message,
                        MsgArgs {}.with ("key", statement.key).with ("kind", nameOf (kind))),
                    statement.keyRange, diagnostics.text (Msg::resolver_unknownKey_label));

                if (const auto suggestion = closestKeyTo (kind, statement.key);
                    ! suggestion.empty())
                    d.helps.push_back (diagnostics.text (Msg::shared_didYouMean_help,
                                                         MsgArgs {}.with ("name", suggestion)));

                continue;
            }

            if (contains (seen, statement.key))
            {
                diagnostics.error ("E205",
                                   diagnostics.text (Msg::resolver_setTwice_message,
                                                     MsgArgs {}.with ("key", statement.key)),
                                   statement.keyRange,
                                   diagnostics.text (Msg::resolver_setTwice_label));
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
            // The two joiners are catalogue entries rather than literals.
            // "a, b and c" is English's shape; a locale that writes it
            // differently - or with no final conjunction at all - changes two
            // strings rather than this loop.
            std::string list;

            for (std::size_t i = 0; i < missing.size(); ++i)
            {
                if (i > 0)
                    list += diagnostics.text (i + 1 == missing.size()
                                                  ? Msg::shared_listFinalSeparator_text
                                                  : Msg::shared_listSeparator_text);

                list += "`" + std::string (missing[i]) + "`";
            }

            diagnostics.error (
                "E206",
                diagnostics.text (Msg::resolver_missingKeys_message,
                                  MsgArgs {}.with ("kind", nameOf (kind)).with ("list", list)),
                block.keywordRange, diagnostics.text (Msg::resolver_missingKeys_label));
        }
    }

    void wrongValue (const Statement& statement, ValueKind kind);

    // --- value readers ------------------------------------------------------
    std::optional<int> asInteger (const Statement& statement);

    std::optional<std::pair<int, int>> asPitchRange (const Statement& statement);

    std::optional<Key> asKey (const Statement& statement);

    /** An enum-shaped value: its index in the schema's member list, which is
        also the enum's own value. One table, two uses.
    */
    std::optional<int> asMemberIndex (const Statement& statement, ValueKind kind);

    /** `N bars` or `N bar`. */
    std::optional<int> asBars (const Statement& statement);

    void noteDuration (Duration duration, SourceRange range);

    /** The name a statement refers to, or "" with `notFound` reported.

        The message is a whole sentence rather than a frame with the noun
        spliced into it. `code` stays a parameter because the same lookup is
        reported under two of them, which is the whole reason a code cannot key
        a catalogue.
    */
    std::string_view resolveName (const Statement& statement,
                                  const std::vector<std::string>& declared, const char* code,
                                  Msg notFound);

    // --- song ---------------------------------------------------------------
    void resolveSong (const Block& block);

    void readTempo (const Statement& statement, const KeySpec& spec);

    void readMeter (const Statement& statement, const KeySpec& spec);

    void readGrid (const Statement& statement, const KeySpec& spec);

    // --- channel ------------------------------------------------------------
    void resolveChannel (const Block& block);

    /** Reads a trailing `per <scope>`, and says whether it was there.

        Returns false only when `per` is present and what follows it is not a
        scope, which is the case worth a diagnostic - `per verse` is a typo, not
        a scope nobody has implemented yet.
    */
    bool readScope (const Statement& statement, std::size_t at, Scope& out);

    void readVelocity (ChannelSpec& channel, const Statement& statement, const KeySpec& spec);

    // --- voicing ------------------------------------------------------------
    void resolveVoicing (const Block& block);

    // --- rhythm -------------------------------------------------------------
    RhythmSpec resolveRhythm (const Block& block);

    // --- harmony ------------------------------------------------------------
    HarmonySpec resolveHarmony (const Block& block);

    void readChordLength (ChordSpec& chord, const ChordEntry& entry);

    // --- section ------------------------------------------------------------
    void resolveSection (const Block& block);

    // --- part ---------------------------------------------------------------
    PartSpec resolvePart (const Block& block);

    /** `leap max 7`, optionally `resolve step` or `resolve free`. */
    void readLeap (MelodySpec& melody, const Statement& statement, const KeySpec& spec);

    /** `cadence 1`, or `cadence choose [1 3 5] per instance`.

        A closed shape rather than an expression: a list of chord-tone degrees
        and a scope. The moment a value can be COMPUTED, completion stops being
        a table lookup and the grid stops being statically knowable, and those
        two properties are what the whole language is built on.
    */
    void readCadence (MelodySpec& melody, const Statement& statement, const KeySpec& spec);

    // --- imitation -----------------------------------------------------------
    /** `imitate lead { delay 1 bar  transpose 5 }`.

        A transformation, not a search. A beam search will essentially never
        DISCOVER imitation, because imitation constrains the whole line's
        identity rather than local transitions - so it is written out, where it
        is exact.
    */
    ImitationSpec resolveImitation (const Block& block, PartSpec& part);

    // --- counterpoint --------------------------------------------------------
    /** `counterpoint against lead { ... }`.

        The header carries `against <channel>`; the parser keeps every word
        between the keyword and the brace, so nothing about this shape needed a
        parser change.
    */
    CounterpointSpec resolveCounterpoint (const Block& block, PartSpec& part);

    static std::optional<CounterpointRule> ruleFor (std::string_view key);

    /** `forbid`, or `soft <weight>`. */
    void readRule (RuleSetting& setting, const Statement& statement, const KeySpec& spec);

    // --- melody -------------------------------------------------------------
    MelodySpec resolveMelody (const Block& block, PartSpec& part);

    void readVariance (MelodySpec& melody, const Statement& statement, const KeySpec& spec);

    void readMuteBudget (MelodySpec& melody, const Statement& statement, const KeySpec& spec);

    // --- arrangement --------------------------------------------------------
    void resolveArrangement (const Block& block);

    static std::string_view closestOfNames (const std::vector<std::string_view>& names,
                                            std::string_view text);

    std::string_view source;
    DiagnosticBag& diagnostics;
    SymbolTable& symbols;
    Model model;
};

} // namespace dew::lang
