#include "lang/Grid.h"

#include <numeric>

#include "lang/ScanCore.h"

namespace dew::lang
{

namespace
{

Duration reduced (int numerator, int denominator) noexcept
{
    const auto divisor = std::gcd (numerator, denominator);
    return { numerator / divisor, denominator / divisor };
}

/** Reads `<digits>/<digits>` and the optional dot and triplet suffixes. Returns
    false on anything else, including a trailing character the language does not
    define - a lexeme is either a whole note value or it is not one.
*/
bool readRatio (std::string_view lexeme, int& numerator, int& denominator,
                bool& dotted, bool& triplet) noexcept
{
    numerator = 0;
    denominator = 0;
    dotted = false;
    triplet = false;

    std::size_t i = 0;

    if (i >= lexeme.size() || ! isDigit (lexeme[i]))
        return false;

    while (i < lexeme.size() && isDigit (lexeme[i]))
        numerator = numerator * 10 + (lexeme[i++] - '0');

    if (i >= lexeme.size() || lexeme[i] != '/')
        return false;

    ++i;

    if (i >= lexeme.size() || ! isDigit (lexeme[i]))
        return false;

    while (i < lexeme.size() && isDigit (lexeme[i]))
        denominator = denominator * 10 + (lexeme[i++] - '0');

    if (i < lexeme.size() && lexeme[i] == '.')
    {
        dotted = true;
        ++i;
    }

    if (i < lexeme.size() && lexeme[i] == 't')
    {
        triplet = true;
        ++i;
    }

    return i == lexeme.size() && numerator > 0 && denominator > 0;
}

} // namespace

std::optional<Duration> parseDuration (std::string_view lexeme) noexcept
{
    int numerator = 0;
    int denominator = 0;
    auto dotted = false;
    auto triplet = false;

    if (! readRatio (lexeme, numerator, denominator, dotted, triplet))
        return std::nullopt;

    // A dot is half again; a triplet is two thirds. Both are exact here and
    // would not be in a double.
    if (dotted)
    {
        numerator *= 3;
        denominator *= 2;
    }

    if (triplet)
    {
        numerator *= 2;
        denominator *= 3;
    }

    return reduced (numerator, denominator);
}

std::optional<TimeSignature> parseTimeSignature (std::string_view lexeme) noexcept
{
    int beats = 0;
    int unit = 0;
    auto dotted = false;
    auto triplet = false;

    if (! readRatio (lexeme, beats, unit, dotted, triplet))
        return std::nullopt;

    // A meter is never dotted and never a triplet; those suffixes make it a
    // duration written where a meter was wanted.
    if (dotted || triplet)
        return std::nullopt;

    return TimeSignature { beats, unit };
}

int gridNeededFor (Duration duration, int beatUnit) noexcept
{
    if (duration.denominator <= 0 || beatUnit <= 0)
        return 1;

    const auto scaled = duration.numerator * beatUnit;
    return duration.denominator / std::gcd (duration.denominator, scaled);
}

int stepsFor (Duration duration, int beatUnit, int stepsPerBeat) noexcept
{
    if (duration.denominator <= 0)
        return 0;

    return duration.numerator * beatUnit * stepsPerBeat / duration.denominator;
}

GridResolution resolveGrid (const std::vector<DurationUse>& uses, int beatUnit) noexcept
{
    GridResolution result;

    if (uses.empty())
        return result;

    auto running = 1;

    for (const auto& use : uses)
        running = std::lcm (running, gridNeededFor (use.duration, beatUnit));

    result.required = running;

    // Now name the witnesses: the pair whose own requirements already multiply
    // out to the whole answer. That is the pair a user has to change. The two
    // LARGEST requirements often are not, because one may already divide the
    // other - 1/16 and 1/32 both want a power of two and neither is the
    // conflict; 1/32 and 1/8t are.
    //
    // A single duration can require the total on its own, in which case there is
    // one witness and no pair.
    for (const auto& use : uses)
    {
        if (gridNeededFor (use.duration, beatUnit) == running)
        {
            result.firstWitness = use;
            break;
        }
    }

    if (! result.firstWitness.has_value())
    {
        for (std::size_t i = 0; i < uses.size() && ! result.secondWitness.has_value(); ++i)
        {
            const auto a = gridNeededFor (uses[i].duration, beatUnit);

            for (std::size_t j = i + 1; j < uses.size(); ++j)
            {
                if (std::lcm (a, gridNeededFor (uses[j].duration, beatUnit)) == running)
                {
                    result.firstWitness = uses[i];
                    result.secondWitness = uses[j];
                    break;
                }
            }
        }
    }

    if (running > maxStepsPerBeat)
    {
        result.exceedsHostLimit = true;
        result.stepsPerBeat = maxStepsPerBeat;
    }
    else
    {
        result.stepsPerBeat = running;
    }

    return result;
}

} // namespace dew::lang
