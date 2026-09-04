#include "ui/McpGrants.h"

namespace dew
{

namespace
{

constexpr juce::juce_wchar separator = '\t';

} // namespace

McpGrants::McpGrants (Settings& s)
    : settings (s)
{
}

std::vector<McpGrants::Entry> McpGrants::all() const
{
    std::vector<Entry> entries;

    juce::StringArray lines;
    lines.addLines (settings.getMcpGrants());

    for (const auto& line : lines)
    {
        if (! line.containsChar (separator))
            continue;

        Entry entry;
        entry.clientName = line.upToFirstOccurrenceOf (juce::String::charToString (separator),
                                                       false, false);
        entry.grant = control::grantFromString (
            line.fromLastOccurrenceOf (juce::String::charToString (separator), false, false));

        // A line this build cannot read is dropped, not guessed at. grantFromString
        // answers `none` for anything it does not recognise, and a stored entry
        // that means nothing is not an entry: keeping it would show the user a
        // client in the allowed list that is not allowed.
        if (entry.clientName.isNotEmpty() && entry.grant != control::Grant::none)
            entries.push_back (entry);
    }

    return entries;
}

control::Grant McpGrants::grantFor (const juce::String& clientName) const
{
    for (const auto& entry : all())
        if (entry.clientName == clientName)
            return entry.grant;

    return control::Grant::none;
}

void McpGrants::setGrant (const juce::String& clientName, control::Grant grant)
{
    // A name carrying the separator cannot round-trip, so it is refused rather
    // than stored mangled - which would grant something to a name nobody has.
    if (clientName.isEmpty() || clientName.containsChar (separator))
        return;

    auto entries = all();
    auto found = false;

    for (auto& entry : entries)
    {
        if (entry.clientName != clientName)
            continue;

        entry.grant = grant;
        found = true;
    }

    if (! found)
        entries.push_back ({ clientName, grant });

    write (entries);
}

void McpGrants::revoke (const juce::String& clientName)
{
    std::vector<Entry> kept;

    for (const auto& entry : all())
        if (entry.clientName != clientName)
            kept.push_back (entry);

    write (kept);
}

void McpGrants::revokeAll()
{
    write ({});
}

void McpGrants::write (const std::vector<Entry>& entries)
{
    juce::StringArray lines;

    for (const auto& entry : entries)
        if (entry.grant != control::Grant::none)
            lines.add (entry.clientName + juce::String::charToString (separator)
                       + control::grantToString (entry.grant));

    settings.setMcpGrants (lines.joinIntoString ("\n"));

    // Written through immediately rather than at the next autosave. This is a
    // security decision the user just made, and losing it to a crash would mean
    // being asked again - or worse, a revocation that quietly did not happen.
    settings.flush();
}

} // namespace dew
