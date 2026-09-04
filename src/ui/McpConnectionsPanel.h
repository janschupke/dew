#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Settings.h"
#include "control/McpServer.h"
#include "ui/McpGrants.h"
#include "ui/primitives/DewControls.h"

namespace dew
{

/** What is listening, and who has been let in.

    The other half of the consent dialog: that one is where a decision is made,
    and this is where it can be seen and taken back. A grant that could only be
    given would be a grant nobody should give.

    It shows the address to hand a client and the exact command that adds it,
    because the alternative is a user reading a port off a dialog and typing it
    into a terminal - and the port is not fixed, since dew takes the next free
    one when its own is busy.

    Modelled on MidiSettingsPanel, which is the closest thing dew has: a list of
    rows in a Viewport, test accessors instead of a driveable dialog, and a
    `preferredWidth`/`preferredHeight` pair so dew_shot can render it bare.
*/
class McpConnectionsPanel : public juce::Component
{
public:
    /** @param server  ASKED FOR each time rather than held, and that is not a
                        style choice: turning the switch off destroys the
                        endpoint, so a panel holding a pointer to it would be
                        holding a dangling one the moment it repainted. Null,
                        or returning null, means it is not running - which the
                        panel says rather than refusing to open, because that is
                        the answer somebody opening this most often wants.
        @param grants   may be null; the list is then empty.
        @param settings where the switch is remembered. Null in dew_shot, which
                        renders the panel with no application around it.
        @param onEnabledChanged  called after the switch is written, so whoever
                        owns the endpoint can start or stop it. The panel does
                        not own one and must not: it is a view.
    */
    McpConnectionsPanel (std::function<control::McpServer*()> server, McpGrants* grants,
                         Settings* settings = nullptr, std::function<void()> onEnabledChanged = {});

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Re-reads the grants and lays out again. */
    void refresh();

    // --- for tests -----------------------------------------------------------
    juce::String getAddressText() const;
    juce::String getCommandText() const;
    int getNumGrantRows() const noexcept
    {
        return (int) rows.size();
    }
    juce::String getGrantRowText (int index) const;
    void revokeRow (int index);

    juce::Button& getEnableButton() noexcept
    {
        return enableButton;
    }

    static constexpr int preferredWidth = 520;
    static constexpr int preferredHeight = 340;

private:
    /** One remembered client, and the button that forgets it. */
    struct Row
    {
        McpGrants::Entry entry;
        std::unique_ptr<DewButton> revoke;
    };

    void rebuildRows();

    std::function<control::McpServer*()> server;
    McpGrants* grants = nullptr;
    Settings* settings = nullptr;
    std::function<void()> onEnabledChanged;

    DewCheckbox enableButton { tr (StringId::mcp_connections_enable) };

    juce::Viewport viewport;
    juce::Component list;
    std::vector<Row> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (McpConnectionsPanel)
};

} // namespace dew
