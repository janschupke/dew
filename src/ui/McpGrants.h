#pragma once

#include <vector>

#include "app/Settings.h"
#include "control/McpServer.h"

namespace dew
{

/** What the user has decided about which clients may control dew, remembered
    between launches.

    In dew_ui rather than beside Settings, and that is the layering rather than
    an accident: a grant is spelled in dew_control's vocabulary, and dew_app
    links dew_model alone so it cannot see one. Settings therefore stores the
    text and this decides what the text means - the same split it already makes
    for a playlist lane's height, which it stores raw because it cannot see the
    size ladder that would clamp it.

    ### The encoding

    One line per client, `name<TAB>grant`. A tab because a client's name is
    whatever it called itself and may contain almost anything a person would
    type - but not a tab, which no client sends and no properties file line
    survives anyway. A line this build cannot parse is DROPPED rather than
    guessed at: a grant read back wrongly is a client authorised by nobody.
*/
class McpGrants : public control::McpServer::GrantStore
{
public:
    explicit McpGrants (Settings&);

    control::Grant grantFor (const juce::String& clientName) const override;
    void setGrant (const juce::String& clientName, control::Grant) override;

    /** Everything remembered, for the settings panel to show. */
    struct Entry
    {
        juce::String clientName;
        control::Grant grant = control::Grant::none;
    };

    std::vector<Entry> all() const;

    /** Forgets one client, so it is asked again the next time it connects. */
    void revoke (const juce::String& clientName);

    void revokeAll();

private:
    void write (const std::vector<Entry>&);

    Settings& settings;
};

} // namespace dew
