#include "lang/SourceRange.h"

#include <algorithm>

namespace dew::lang
{

std::string_view SourceRange::textIn (std::string_view source) const noexcept
{
    const auto size = (std::uint32_t) source.size();
    const auto from = std::min (begin, size);
    const auto to = std::min (std::max (end, from), size);

    return source.substr (from, to - from);
}

// ------------------------------------------------------------------------------

LineIndex::LineIndex (std::string_view source)
    : text (source)
{
    lineStarts.push_back (0);

    for (std::uint32_t i = 0; i < (std::uint32_t) source.size(); ++i)
        if (source[i] == '\n')
            lineStarts.push_back (i + 1);
}

int LineIndex::lineAt (std::uint32_t byteOffset) const noexcept
{
    // The first line start STRICTLY greater than the offset; the line is the one
    // before it. upper_bound rather than lower_bound so an offset sitting
    // exactly on a line start belongs to that line and not the previous one.
    const auto it = std::upper_bound (lineStarts.begin(), lineStarts.end(), byteOffset);
    return (int) std::distance (lineStarts.begin(), it);
}

int LineIndex::columnAt (std::uint32_t byteOffset) const noexcept
{
    const auto line = lineAt (byteOffset);
    const auto start = lineStarts[(std::size_t) line - 1];
    const auto stop = std::min (byteOffset, (std::uint32_t) text.size());

    // Count characters, not bytes: a UTF-8 continuation byte is 10xxxxxx and is
    // part of the character before it, never one of its own.
    int column = 1;

    for (auto i = start; i < stop; ++i)
        if ((static_cast<unsigned char> (text[i]) & 0xC0) != 0x80)
            ++column;

    return column;
}

std::string_view LineIndex::lineTextAt (std::uint32_t byteOffset) const noexcept
{
    const auto line = lineAt (byteOffset);
    const auto start = lineStarts[(std::size_t) line - 1];

    auto stop = (std::size_t) start;

    while (stop < text.size() && text[stop] != '\n')
        ++stop;

    // Drop a trailing carriage return, so a CRLF file reports the same line text
    // as an LF one rather than one character longer.
    if (stop > start && text[stop - 1] == '\r')
        --stop;

    return text.substr (start, stop - start);
}

} // namespace dew::lang
