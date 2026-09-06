// =============================================================================
// The melody block.
//
// One of four translation units defining lang/ResolverImpl.h.
//
// A melody is the part kind with the most to say about itself: a contour, a
// leap rule, a cadence, how much it varies, and how many notes it may leave
// out. Each of those is a value shape nothing else in the language uses, which
// is what separates it from the part block that dispatches to it.
// =============================================================================

#include "lang/Resolver.h"

#include "lang/ResolverImpl.h"

#include <algorithm>

#include "lang/Counterpoint.h"
#include "lang/Numbers.h"
#include "lang/ScanCore.h"

namespace dew::lang
{

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
        auto& d = diagnostics.error ("E247", diagnostics.text (Msg::melody_leapRange_message),
                                     values[1].range);
        d.helps.push_back (diagnostics.text (Msg::melody_leapRange_help));
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
            auto& d = diagnostics.error (
                "E252", diagnostics.text (Msg::melody_cadenceChordTone_message), value.range);
            d.helps.push_back (diagnostics.text (Msg::melody_cadenceChordTone_help));
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
        diagnostics.error ("E253", diagnostics.text (Msg::melody_listNeverClosed_message),
                           statement.range, diagnostics.text (Msg::melody_listNeverClosed_label));
        return;
    }

    if (degrees.empty())
    {
        diagnostics.error ("E243", diagnostics.text (Msg::melody_choiceIsEmpty_message),
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

MelodySpec Resolver::resolveMelody (const Block& block, PartSpec& part)
{
    MelodySpec melody;

    forEachStatement (block, BlockKind::melody,
                      [&] (const KeySpec& spec, const Statement& statement)
                      {
                          if (statement.key == "rhythm")
                          {
                              const auto name = resolveName (statement, symbols.rhythms, "E233",
                                                             Msg::resolver_noRhythmCalled_message);

                              if (! name.empty())
                                  melody.rhythm = std::string (name);
                              else if (statement.values.size() != 1)
                                  wrongValue (statement, spec.kind);
                          }
                          else if (statement.key == "articulation")
                          {
                              assignMember (statement, spec.kind, melody.articulation);
                          }
                          else if (statement.key == "contour")
                          {
                              assignMember (statement, spec.kind, melody.contour);
                          }
                          else if (statement.key == "strong")
                          {
                              assignMember (statement, spec.kind, melody.strong);
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
                              assignMember (statement, spec.kind, melody.align);
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
                               diagnostics.text (Msg::melody_notPartOfMelody_message,
                                                 MsgArgs {}.with ("keyword", child.keyword)),
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
        auto& d = diagnostics.error ("E234", diagnostics.text (Msg::melody_varianceRange_message),
                                     statement.range);
        d.notes.push_back (diagnostics.text (Msg::melody_varianceRange_note));
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
        auto& d = diagnostics.error ("E235", diagnostics.text (Msg::melody_muteBudget_message),
                                     statement.range);
        d.notes.push_back (
            diagnostics.text (Msg::melody_muteBudget_note,
                              MsgArgs {}.with ("count", *count).with ("window", *window)));
        return;
    }

    melody.muteCount = (int) *count;
    melody.muteWindow = (int) *window;
}
} // namespace dew::lang
