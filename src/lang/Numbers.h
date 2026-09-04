#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dew::lang
{

/** Reading a number, and asking whether a name is in a list.

    File-local at the top of Resolver.cpp until that file was the largest in the
    tree. They belong to nothing in the resolver: each takes a string_view and
    answers an optional, touching no diagnostics and no symbol table.

    The two readers are hand-rolled rather than from_chars or strtod, and
    deliberately. The point is that both agree on what a trailing character
    means - "4x" is not four - and that neither is locale-sensitive, because
    the score language must read the same on every machine. See Rng.h for the
    other half of that promise.
*/
bool contains (const std::vector<std::string>& names, std::string_view name);
bool contains (const std::vector<std::string_view>& names, std::string_view name);

/** A decimal or 0x-prefixed integer, or nothing if the whole text is not one. */
std::optional<long long> readInteger (std::string_view text);

/** A decimal number with an optional fraction, accumulated in double by hand. */
std::optional<double> readNumber (std::string_view text);

} // namespace dew::lang
