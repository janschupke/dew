#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace dew::lang
{

/** A half-open span of the source, in BYTES.

    Bytes rather than line/column because that is what a compiler naturally
    produces and what an editor needs to build a caret position: line and column
    are a rendering concern, computed on demand by LineIndex.

    Bytes rather than characters for the same reason a file offset is bytes - the
    lexer walks a std::string_view. The editor is the one place that has to care
    about the difference, and it converts once, deliberately, rather than every
    layer guessing. A source with one em dash in a comment puts every later
    character index two out of step with its byte offset.
*/
struct SourceRange
{
    std::uint32_t begin = 0;
    std::uint32_t end = 0;

    constexpr bool isEmpty() const noexcept
    {
        return end <= begin;
    }
    constexpr std::uint32_t length() const noexcept
    {
        return isEmpty() ? 0u : end - begin;
    }

    constexpr bool operator== (const SourceRange& other) const noexcept
    {
        return begin == other.begin && end == other.end;
    }

    constexpr bool operator!= (const SourceRange& other) const noexcept
    {
        return ! operator== (other);
    }

    /** The text this range covers. Clamped, so a range built from a stale
        document yields a short string rather than reading past the end.
    */
    std::string_view textIn (std::string_view source) const noexcept;
};

/** Line starts, so a byte offset can be reported as a line and a column.

    Built once per compile and shared by every diagnostic, because the
    alternative - each diagnostic scanning the source for newlines - is
    quadratic in the number of errors, and a file with a hundred of them is
    exactly when you want the compiler to still feel instant.
*/
class LineIndex
{
public:
    explicit LineIndex (std::string_view source);

    /** 1-based line number containing this byte offset. */
    int lineAt (std::uint32_t byteOffset) const noexcept;

    /** 1-based column, counted in CHARACTERS rather than bytes, so a diagnostic
        printed under a line with a multi-byte character still puts its caret
        where the eye expects.
    */
    int columnAt (std::uint32_t byteOffset) const noexcept;

    /** The whole line containing this offset, without its newline - what a
        diagnostic prints above its caret.
    */
    std::string_view lineTextAt (std::uint32_t byteOffset) const noexcept;

    int lineCount() const noexcept
    {
        return (int) lineStarts.size();
    }

private:
    std::string_view text;
    std::vector<std::uint32_t> lineStarts;
};

} // namespace dew::lang
