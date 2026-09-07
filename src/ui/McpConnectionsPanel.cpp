#include "i18n/Strings.h"
#include "ui/McpConnectionsPanel.h"
#include "ui/design/Tokens.h"

namespace dew
{

namespace
{

/** What a grant is called, where a person reads it.

    A switch with no default: -Wswitch-enum is an error under the ci preset, so
    a grant added to the enum fails to compile until somebody has said what the
    settings panel calls it - which is better than a row that reads as blank.
*/
StringId nameOfGrant (control::Grant grant)
{
    switch (grant)
    {
        case control::Grant::read: return StringId::mcp_connections_grantRead;
        case control::Grant::readWrite: return StringId::mcp_connections_grantWrite;
        case control::Grant::none: break;
    }

    return StringId::mcp_connections_offline;
}

} // namespace

McpConnectionsPanel::McpConnectionsPanel (std::function<control::McpServer*()> s,
                                          std::function<McpGrants*()> g, Settings* st,
                                          std::function<void()> onChanged)
    : server (std::move (s))
    , grants (std::move (g))
    , settings (st)
    , onEnabledChanged (std::move (onChanged))
{
    setComponentID ("mcpConnectionsPanel");

    enableButton.setToggleState (settings != nullptr && settings->getMcpEnabled(),
                                 juce::dontSendNotification);

    enableButton.onClick = [this, self = juce::Component::SafePointer<McpConnectionsPanel> (this)]
    {
        if (settings != nullptr)
            settings->setMcpEnabled (enableButton.getToggleState());

        // Written, then acted on, then shown. The panel does not own the
        // endpoint - it says what the endpoint is doing - so starting or
        // stopping one is the caller's, and the address it then displays is
        // read back rather than predicted.
        if (onEnabledChanged)
            onEnabledChanged();

        // Checked, because the line above is a call OUT of this panel into the
        // application, and an application is entitled to close a window while
        // answering it. Everything refresh touches is fetched fresh for the
        // same reason; this covers the panel itself.
        if (self == nullptr)
            return;

        refresh();
    };

    addAndMakeVisible (enableButton);

    // Both of these exist to be TAKEN somewhere else - pasted into a client's
    // config, or run in a terminal - and both were drawn text until now, which
    // a person could read off the screen and retype and nothing more.
    addressField.setTooltip (tr (StringId::mcp_connections_address));
    addressField.setCopyTooltip (tr (StringId::mcp_connections_copyAddress));
    addAndMakeVisible (addressField);

    commandField.setFont (tokens::type::monospaced (tokens::type::caption));
    commandField.setTooltip (tr (StringId::mcp_connections_command));
    commandField.setCopyTooltip (tr (StringId::mcp_connections_copyCommand));
    addChildComponent (commandField);

    viewport.setViewedComponent (&list, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    updateFields();
    rebuildRows();

    setSize (preferredWidth, preferredHeight);
}

juce::String McpConnectionsPanel::getAddressText() const
{
    auto* running = server ? server() : nullptr;

    if (running == nullptr || ! running->isRunning())
        return tr (StringId::mcp_connections_offline);

    return running->getUrl();
}

juce::String McpConnectionsPanel::getCommandText() const
{
    auto* running = server ? server() : nullptr;

    if (running == nullptr || ! running->isRunning())
        return {};

    // Spelled out rather than described. The port is not fixed - dew takes the
    // next free one when its own is busy - so telling somebody to "add the URL
    // above" means them reading a number off a dialog and typing it correctly.
    //
    // NOT a catalogue row. It is a command typed into a terminal, and it is the
    // same characters in every language - translating it would break it. Named
    // shellCommand because that is what the gate on English strips on: a shape,
    // rather than this file's name in an exemption list.
    constexpr const char* shellCommand = "claude mcp add --transport http dew ";

    return shellCommand + running->getUrl();
}

juce::String McpConnectionsPanel::getGrantRowText (int index) const
{
    if (index < 0 || index >= (int) rows.size())
        return {};

    const auto& row = rows[(size_t) index];

    return tr (StringId::mcp_connections_grantRow,
               Args {}
                   .with ("client", row.entry.clientName)
                   .with ("grant", tr (nameOfGrant (row.entry.grant))));
}

McpGrants* McpConnectionsPanel::grantsNow() const
{
    return grants ? grants() : nullptr;
}

void McpConnectionsPanel::revokeRow (int index)
{
    if (index < 0 || index >= (int) rows.size())
        return;

    auto* store = grantsNow();

    if (store == nullptr)
        return;

    store->revoke (rows[(size_t) index].entry.clientName);
    refresh();
}

void McpConnectionsPanel::refresh()
{
    // Before the layout, not after: whether there is a command decides whether
    // the command block is on screen at all, and layOut asks.
    updateFields();
    rebuildRows();
    resized();
    repaint();
}

void McpConnectionsPanel::rebuildRows()
{
    rows.clear();
    list.removeAllChildren();

    auto* store = grantsNow();

    if (store == nullptr)
        return;

    for (const auto& entry : store->all())
    {
        Row row;
        row.entry = entry;
        row.revoke = std::make_unique<DewButton> (tr (StringId::mcp_connections_revoke),
                                                  DewButton::Role::ghost);

        // The client's NAME rather than the row's index, because revoking one
        // rebuilds the list and every index after it means something else.
        const auto name = entry.clientName;
        row.revoke->onClick =
            [this, name, self = juce::Component::SafePointer<McpConnectionsPanel> (this)]
        {
            if (auto* current = grantsNow())
                current->revoke (name);

            // POSTED, not called. refresh() rebuilds the rows, and this button
            // is one of them - so calling it here would destroy the std::function
            // that is executing, out from under its own captures. The chain
            // reorder makes the same move for the same reason: report the
            // gesture, then touch nothing of yourself.
            juce::MessageManager::callAsync (
                [self]
                {
                    if (self != nullptr)
                        self->refresh();
                });
        };

        list.addAndMakeVisible (*row.revoke);
        rows.push_back (std::move (row));
    }
}

McpConnectionsPanel::Layout McpConnectionsPanel::layOut() const
{
    using namespace tokens;

    Layout out;
    auto area = contentBounds();

    const auto running = getCommandText().isNotEmpty();

    out.enable = area.removeFromTop (size::controlHeight);
    out.enableHelp = area.removeFromTop (size::controlHeightSm);
    area.removeFromTop (space::md);

    out.addressCaption = area.removeFromTop (size::controlHeightSm);
    out.address = area.removeFromTop (size::controlHeight);

    // Only when there IS an address. "Give this to an MCP client" under the
    // words "Not running" is an instruction about nothing.
    if (running)
        out.addressHelp = area.removeFromTop (size::controlHeightSm);

    area.removeFromTop (space::md);

    if (running)
    {
        out.commandCaption = area.removeFromTop (size::controlHeightSm);
        out.command = area.removeFromTop (size::controlHeight);
        area.removeFromTop (space::md);
    }

    out.grantedCaption = area.removeFromTop (size::controlHeightSm);
    out.list = area;

    return out;
}

void McpConnectionsPanel::updateFields()
{
    const auto command = getCommandText();

    addressField.setText (getAddressText());

    // The address field shows "Not running" when there is no endpoint, which
    // is a state rather than an address - so there is nothing to take.
    addressField.setCopyable (command.isNotEmpty());

    commandField.setText (command);
    commandField.setVisible (command.isNotEmpty());
}

void McpConnectionsPanel::paint (juce::Graphics& g)
{
    using namespace tokens;

    paintBackground (g);

    const auto layout = layOut();

    // The captions only. What they caption is a component now - a field you can
    // select from and copy out of - rather than a string drawn into the gap
    // underneath, which could be read and nothing else.
    g.setColour (colour::textSecondary);
    g.setFont (type::font (type::caption));

    const auto caption = [&g] (const juce::String& text, juce::Rectangle<int> bounds)
    {
        if (! bounds.isEmpty())
            g.drawFittedText (text, bounds, juce::Justification::centredLeft, 1);
    };

    caption (tr (StringId::mcp_connections_enableHelp), layout.enableHelp);
    caption (tr (StringId::mcp_connections_address), layout.addressCaption);
    caption (tr (StringId::mcp_connections_addressHelp), layout.addressHelp);
    caption (tr (StringId::mcp_connections_command), layout.commandCaption);
    caption (tr (StringId::mcp_connections_granted), layout.grantedCaption);

    if (rows.empty())
    {
        g.setColour (colour::textSecondary);
        g.setFont (type::font (type::body));
        g.drawFittedText (tr (StringId::mcp_connections_empty), layout.list,
                          juce::Justification::topLeft, 2);
    }
}

void McpConnectionsPanel::resized()
{
    using namespace tokens;

    const auto layout = layOut();

    enableButton.setBounds (layout.enable);
    addressField.setBounds (layout.address);

    if (! layout.command.isEmpty())
        commandField.setBounds (layout.command);

    const auto area = layout.list;
    viewport.setBounds (area);

    const auto rowHeight = size::controlHeight + space::xs;
    list.setSize (area.getWidth(), juce::jmax (area.getHeight(), (int) rows.size() * rowHeight));

    auto y = 0;

    for (auto& row : rows)
    {
        juce::Rectangle<int> bounds { 0, y, list.getWidth(), size::controlHeight };
        row.revoke->setBounds (bounds.removeFromRight (dialog::buttonWidthFor (*row.revoke)));
        y += rowHeight;
    }
}

} // namespace dew
