#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "lang/Music.h"
#include "lang/Schema.h"
#include "DocsJson.h"

namespace dew::docs
{

/** Every ValueKind, as the identifier the language spells it with.

    lang::nameOf (ValueKind) is what a DIAGNOSTIC says - "a tempo, like `96
    bpm`" - and it is not injective: number and seed both answer "a number". So
    it cannot be a key, and the spelling is written out here.

    A switch with no default, deliberately. -Wswitch-enum is an error under the
    ci preset, so a ValueKind added to Schema.h fails to COMPILE this file until
    somebody names it. That is the closest a restated table gets to being a link
    error, and it is the reason this is not a std::map.
*/
inline const char* identifierOf (lang::ValueKind kind)
{
    // clang-format off
    switch (kind)
    {
        case lang::ValueKind::text:          return "text";
        case lang::ValueKind::integer:       return "integer";
        case lang::ValueKind::number:        return "number";
        case lang::ValueKind::tempo:         return "tempo";
        case lang::ValueKind::meter:         return "meter";
        case lang::ValueKind::grid:          return "grid";
        case lang::ValueKind::key:           return "key";
        case lang::ValueKind::seed:          return "seed";
        case lang::ValueKind::pitchRange:    return "pitchRange";
        case lang::ValueKind::bars:          return "bars";
        case lang::ValueKind::jitteredInt:   return "jitteredInt";
        case lang::ValueKind::voices:        return "voices";
        case lang::ValueKind::muteBudget:    return "muteBudget";
        case lang::ValueKind::spread:        return "spread";
        case lang::ValueKind::motion:        return "motion";
        case lang::ValueKind::contour:       return "contour";
        case lang::ValueKind::strongRule:    return "strongRule";
        case lang::ValueKind::articulation:  return "articulation";
        case lang::ValueKind::lineSource:    return "lineSource";
        case lang::ValueKind::cadence:       return "cadence";
        case lang::ValueKind::rule:          return "rule";
        case lang::ValueKind::bassRule:      return "bassRule";
        case lang::ValueKind::leapRule:      return "leapRule";
        case lang::ValueKind::alignment:     return "alignment";
        case lang::ValueKind::transposeMode: return "transposeMode";
        case lang::ValueKind::scope:         return "scope";
        case lang::ValueKind::instrument:    return "instrument";
        case lang::ValueKind::rhythmRef:     return "rhythmRef";
        case lang::ValueKind::voicingRef:    return "voicingRef";
        case lang::ValueKind::harmonyRef:    return "harmonyRef";
        case lang::ValueKind::channelRef:    return "channelRef";
    }

    // clang-format on
    return "unknown";
}

/** Every ValueKind, in declaration order.

    ALL of them, not only the ones a key's `kind` names. Two are reached through
    a block HEADER rather than a KeySpec - `channelRef` by `counterpoint against
    lead` and `imitate lead`, and `scope` by the `per bar` / `per instance`
    suffix - so a list collected by walking the keys would silently drop two
    real vocabularies, one of which the README documents at length.

    Written out because C++ cannot walk an enum. It is kept honest by
    identifierOf above rather than by a count here: -Wswitch-enum is an error
    under the ci preset, so a new ValueKind fails to compile that switch, and
    the comment there sends the author to this list. A test then holds every
    entry to a unique, non-"unknown" identifier, which is what catches the
    author who added the case and forgot the row.
*/
inline std::vector<lang::ValueKind> allKinds()
{
    // clang-format off
    return { lang::ValueKind::text,          lang::ValueKind::integer,
             lang::ValueKind::number,        lang::ValueKind::tempo,
             lang::ValueKind::meter,         lang::ValueKind::grid,
             lang::ValueKind::key,           lang::ValueKind::seed,
             lang::ValueKind::pitchRange,    lang::ValueKind::bars,
             lang::ValueKind::jitteredInt,   lang::ValueKind::voices,
             lang::ValueKind::muteBudget,    lang::ValueKind::spread,
             lang::ValueKind::motion,        lang::ValueKind::contour,
             lang::ValueKind::strongRule,    lang::ValueKind::articulation,
             lang::ValueKind::lineSource,    lang::ValueKind::cadence,
             lang::ValueKind::rule,          lang::ValueKind::bassRule,
             lang::ValueKind::leapRule,      lang::ValueKind::alignment,
             lang::ValueKind::transposeMode, lang::ValueKind::scope,
             lang::ValueKind::instrument,    lang::ValueKind::rhythmRef,
             lang::ValueKind::voicingRef,    lang::ValueKind::harmonyRef,
             lang::ValueKind::channelRef };
    // clang-format on
}

/** Every mode, in declaration order. Music.h states these and nothing a reader
    can see does - the completion popup offers them and then closes.
*/
inline std::vector<lang::Mode> allModes()
{
    // clang-format off
    return { lang::Mode::major,           lang::Mode::minor,
             lang::Mode::dorian,          lang::Mode::phrygian,
             lang::Mode::lydian,          lang::Mode::mixolydian,
             lang::Mode::locrian,         lang::Mode::harmonicMinor,
             lang::Mode::melodicMinor,    lang::Mode::majorPentatonic,
             lang::Mode::minorPentatonic, lang::Mode::blues,
             lang::Mode::chromatic };
    // clang-format on
}

/** The score language as JSON, from lang::schema() and nothing else.

    The schema's own doc comment says the reference manual reads this table, so
    that a key cannot exist without being completable and cannot be documented
    differently from how it is checked. This function is that reader.

    The locale is a parameter with the REFERENCE as its default, and dew_docs
    passes nothing. website/src/generated/score-schema.json is committed and
    compared byte for byte in three places, so what it holds has to be a
    function of the schema alone - never of anything a machine happened to be
    set to. A per-locale reference page is one argument away the day it is
    wanted, and nothing here decides that.
*/
inline std::string schemaJson (lang::Locale locale = lang::referenceLocale)
{
    JsonWriter json;

    json.beginObject();

    json.key ("valueKinds");
    json.beginArray();

    for (const auto kind : allKinds())
    {
        json.beginObject();
        json.key ("name");
        json.value (identifierOf (kind));
        json.key ("doc");
        json.value (lang::msg (lang::nameOf (kind), locale));

        json.key ("members");
        json.beginArray();

        for (const auto& member : lang::membersOf (kind))
            json.value (member);

        json.endArray();
        json.endObject();
    }

    json.endArray();

    json.key ("blocks");
    json.beginArray();

    for (const auto& block : lang::schema())
    {
        json.beginObject();
        json.key ("kind");
        json.value (lang::nameOf (block.kind));
        json.key ("doc");
        json.value (lang::msg (block.doc, locale));
        json.key ("topLevel");
        json.value (block.topLevel);

        json.key ("keys");
        json.beginArray();

        for (const auto& key : block.keys)
        {
            json.beginObject();
            json.key ("name");
            json.value (key.name);
            json.key ("kind");
            json.value (identifierOf (key.kind));

            // The kind's prose is repeated onto the key so a reference row can
            // be rendered without a second lookup. `members` stays in one
            // place, above, because that is the part a reader scans.
            json.key ("kindDoc");
            json.value (lang::msg (lang::nameOf (key.kind), locale));
            json.key ("required");
            json.value (key.required);
            json.key ("overridable");
            json.value (key.overridable);
            json.key ("doc");
            json.value (lang::msg (key.doc, locale));
            json.endObject();
        }

        json.endArray();

        json.key ("children");
        json.beginArray();

        for (const auto child : block.children)
            json.value (lang::nameOf (child));

        json.endArray();
        json.endObject();
    }

    json.endArray();

    json.key ("modes");
    json.beginArray();

    for (const auto mode : allModes())
    {
        json.beginObject();
        json.key ("name");
        json.value (lang::nameOf (mode));

        json.key ("degrees");
        json.beginArray();

        for (const auto degree : lang::degreesOf (mode))
            json.value (degree);

        json.endArray();

        // A roman numeral names a scale DEGREE, so a pentatonic or a blues
        // scale cannot carry one. Saying so is the difference between a
        // reference and a list.
        json.key ("romanNumerals");
        json.value (lang::supportsRomanNumerals (mode));
        json.endObject();
    }

    json.endArray();
    json.endObject();

    return json.str();
}

} // namespace dew::docs
