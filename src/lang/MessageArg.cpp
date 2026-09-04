#include "lang/MessageArg.h"

#include <cassert>

namespace dew::lang
{

std::string MsgArg::toDisplayString() const
{
    switch (kind)
    {
        case Kind::text: return text;
        case Kind::integer: return std::to_string (number);
        case Kind::none: break;
    }

    return {};
}

MsgArgs& MsgArgs::with (const char* name, MsgArg value)
{
    // Silently dropping the seventh argument would be a brace in a diagnostic
    // with no way to find out why. Nothing in dew comes close to six.
    assert (size < capacity);

    if (size >= capacity)
        return *this;

    auto& entry = entries[size++];

    // Copied, not pointed at - see the note in MsgArgs. Truncation would make
    // two long names compare equal, so it is an assertion rather than a silent
    // cut.
    auto length = 0;

    while (name != nullptr && name[length] != '\0' && length < maximumNameLength - 1)
    {
        entry.name[length] = name[length];
        ++length;
    }

    assert (name == nullptr || name[length] == '\0');

    entry.name[length] = '\0';
    entry.value = std::move (value);

    return *this;
}

const MsgArg* MsgArgs::find (std::string_view name) const noexcept
{
    for (auto i = 0; i < size; ++i)
        if (name == entries[i].name)
            return &entries[i].value;

    return nullptr;
}

} // namespace dew::lang
