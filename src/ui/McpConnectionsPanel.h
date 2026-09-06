#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "i18n/Strings.h"
#include "app/Settings.h"
#include "control/McpServer.h"
#include "ui/DewDialog.h"
#include "ui/McpGrants.h"
#include "ui/primitives/DewButtons.h"
#include "ui/primitives/DewReadOnlyField.h"

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
class McpConnectionsPanel : public dialog::Panel
{
public:
    /** @param server  ASKED FOR each time rather than held, and that is not a
                        style choice: turning the switch off destroys the
                        endpoint, so a panel holding a pointer to it would be
                        holding a dangling one the moment it repainted. Null,
                        or returning null, means it is not running - which the
                        panel says rather than refusing to open, because that is
                        the answer somebody opening this most often wants.
        @param grants   ASKED FOR too, and for the same reason rather than a
                        weaker one: the switch on this panel calls out to the
                        application and the application is entitled to rebuild
                        what it owns, so anything held across that call is held
                        across a call that can free it. It used to be a bare
                        pointer and ticking the switch segfaulted dew. Null, or
                        returning null, means the list is empty.
        @param settings where the switch is remembered. Null in dew_shot, which
                        renders the panel with no application around it.
        @param onEnabledChanged  called after the switch is written, so whoever
                        owns the endpoint can start or stop it. The panel does
                        not own one and must not: it is a view.
    */
    McpConnectionsPanel (std::function<control::McpServer*()> server,
                         std::function<McpGrants*()> grants, Settings* settings = nullptr,
                         std::function<void()> onEnabledChanged = {});

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

    /** The two fields whose whole purpose is to end up somewhere else, so a
        test can select from and copy what a person selects from and copies. */
    DewReadOnlyField& getAddressField() noexcept
    {
        return addressField;
    }
    DewReadOnlyField& getCommandField() noexcept
    {
        return commandField;
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

    /** Where every part of the header goes.

        Computed once and read by BOTH paint() and resized(), which each used to
        replay the other's vertical arithmetic - including the same
        "is there a command" conditional, spelled twice in one and twice in the
        other. Two walks down one column is a layout that drifts the first time
        a line is added to either.
    */
    struct Layout
    {
        juce::Rectangle<int> enable, enableHelp;
        juce::Rectangle<int> addressCaption, address, addressHelp;
        juce::Rectangle<int> commandCaption, command;
        juce::Rectangle<int> grantedCaption, list;
    };

    Layout layOut() const;

    /** Puts the current address and command into the fields, and hides the
        command block when there is nothing listening - "Give this to an MCP
        client" under the words "Not running" is an instruction about nothing. */
    void updateFields();

    /** The grants right now, or nullptr. One place asks, so the "is there a
        function, and did it answer" pair is written once. */
    McpGrants* grantsNow() const;

    std::function<control::McpServer*()> server;
    std::function<McpGrants*()> grants;
    Settings* settings = nullptr;
    std::function<void()> onEnabledChanged;

    DewCheckbox enableButton { tr (StringId::mcp_connections_enable) };

    DewReadOnlyField addressField, commandField;

    juce::Viewport viewport;
    juce::Component list;
    std::vector<Row> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (McpConnectionsPanel)
};

} // namespace dew
