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

    viewport.setViewedComponent (&list, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

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
    return "claude mcp add --transport http dew " + running->getUrl();
}

juce::String McpConnectionsPanel::getGrantRowText (int index) const
{
    if (index < 0 || index >= (int) rows.size())
        return {};

    const auto& row = rows[(size_t) index];

    return row.entry.clientName + " - " + tr (nameOfGrant (row.entry.grant));
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

void McpConnectionsPanel::paint (juce::Graphics& g)
{
    using namespace tokens;

    paintBackground (g);

    auto area = contentBounds();

    area.removeFromTop (size::controlHeight);

    g.setColour (colour::textSecondary);
    g.setFont (type::font (type::caption));
    g.drawFittedText (tr (StringId::mcp_connections_enableHelp),
                      area.removeFromTop (size::controlHeightSm), juce::Justification::centredLeft,
                      1);

    area.removeFromTop (space::md);

    g.drawFittedText (tr (StringId::mcp_connections_address),
                      area.removeFromTop (size::controlHeightSm), juce::Justification::centredLeft,
                      1);

    g.setColour (colour::textPrimary);
    g.setFont (type::font (type::body));
    g.drawFittedText (getAddressText(), area.removeFromTop (size::controlHeight),
                      juce::Justification::centredLeft, 1);

    // Only when there IS an address. "Give this to an MCP client" under the
    // words "Not running" is an instruction about nothing.
    if (getCommandText().isNotEmpty())
    {
        g.setColour (colour::textSecondary);
        g.setFont (type::font (type::caption));
        g.drawFittedText (tr (StringId::mcp_connections_addressHelp),
                          area.removeFromTop (size::controlHeightSm),
                          juce::Justification::centredLeft, 1);
    }

    area.removeFromTop (space::md);

    if (const auto command = getCommandText(); command.isNotEmpty())
    {
        g.setColour (colour::textSecondary);
        g.setFont (type::font (type::caption));
        g.drawFittedText (tr (StringId::mcp_connections_command),
                          area.removeFromTop (size::controlHeightSm),
                          juce::Justification::centredLeft, 1);

        g.setColour (colour::textPrimary);
        g.setFont (type::monospaced (type::caption));
        g.drawFittedText (command, area.removeFromTop (size::controlHeight),
                          juce::Justification::centredLeft, 1);

        area.removeFromTop (space::md);
    }

    g.setColour (colour::textSecondary);
    g.setFont (type::font (type::caption));
    g.drawFittedText (tr (StringId::mcp_connections_granted),
                      area.removeFromTop (size::controlHeightSm), juce::Justification::centredLeft,
                      1);

    if (rows.empty())
    {
        g.setColour (colour::textSecondary);
        g.setFont (type::font (type::body));
        g.drawFittedText (tr (StringId::mcp_connections_empty), area, juce::Justification::topLeft,
                          2);
    }
}

void McpConnectionsPanel::resized()
{
    using namespace tokens;

    auto area = contentBounds();

    enableButton.setBounds (area.removeFromTop (size::controlHeight));

    // The header block the painter draws, skipped in the same order it draws
    // it: the switch's explanation, the address and its help, then the command
    // block when there is one and the "allowed" caption below.
    area.removeFromTop (size::controlHeightSm + space::md);
    area.removeFromTop (size::controlHeightSm + size::controlHeight + space::md);

    // The address's help line is drawn only when there is an address, so the
    // layout skips exactly what the painter drew.
    if (getCommandText().isNotEmpty())
        area.removeFromTop (size::controlHeightSm);

    if (getCommandText().isNotEmpty())
        area.removeFromTop (size::controlHeightSm + size::controlHeight + space::md);

    area.removeFromTop (size::controlHeightSm);

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
