#include "lang/Numbers.h"

#include "lang/ScanCore.h"

#include <algorithm>

namespace dew::lang
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

            if (c >= '0' && c <= '9')
                digit = c - '0';
            else if (c >= 'a' && c <= 'f')
                digit = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F')
                digit = c - 'A' + 10;
            else
                return std::nullopt;

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
} // namespace dew::lang
