#include "ui/DewDialog.h"
#include "ui/McpConsentPanel.h"
#include "ui/design/Tokens.h"

namespace dew
{

McpConsentPanel::McpConsentPanel (Request r)
    : request (std::move (r))
{
    setComponentID ("mcpConsentPanel");

    denyButton.onClick = [this] { answer (control::Grant::none); };
    readButton.onClick = [this] { answer (control::Grant::read); };
    writeButton.onClick = [this] { answer (control::Grant::readWrite); };

    addAndMakeVisible (denyButton);
    addAndMakeVisible (readButton);
    addAndMakeVisible (writeButton);

    setSize (preferredWidth, preferredHeight);
}

void McpConsentPanel::answer (control::Grant grant)
{
    // Taken before closing, the way ConfirmPanel does: closing deletes this
    // component, so nothing of it may be touched afterwards - including the
    // callback, which is a member.
    auto reply = onAnswered;
    onAnswered = nullptr;

    if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>())
        dialog->exitModalState (0);

    if (reply)
        reply (grant);
}

void McpConsentPanel::show (Request r, juce::Component* parent,
                            std::function<void (control::Grant)> onAnswered)
{
    auto* panel = new McpConsentPanel (std::move (r));

    // A closed window is a refusal. Without this the socket thread that asked
    // would wait out its whole consent timeout for an answer that is never
    // coming, and the client would see nothing at all for three minutes.
    auto reply = std::move (onAnswered);
    auto answered = std::make_shared<bool> (false);

    panel->onAnswered = [reply, answered] (control::Grant grant)
    {
        *answered = true;

        if (reply)
            reply (grant);
    };

    dialog::launch (panel, tr (StringId::mcp_consent_title), parent);
}

void McpConsentPanel::paint (juce::Graphics& g)
{
    using namespace tokens;

    g.fillAll (colour::background);

    auto area = getLocalBounds().reduced (space::xl);
    area.removeFromBottom (size::controlHeight + space::xl);

    auto question = area.removeFromTop (size::controlHeight);

    g.setColour (colour::textPrimary);
    g.setFont (type::font (type::title));

    // The client's own name for itself, which is a claim rather than a proof -
    // and the reason a person is being asked at all.
    g.drawFittedText (
        tr (StringId::mcp_consent_question, Args {}.with ("client", request.clientName)), question,
        juce::Justification::topLeft, 2);

    area.removeFromTop (space::md);

    g.setColour (colour::textSecondary);
    g.setFont (type::font (type::body));

    g.drawFittedText (tr (StringId::mcp_consent_body), area, juce::Justification::topLeft, 4);
}

void McpConsentPanel::resized()
{
    using namespace tokens;

    auto area = getLocalBounds().reduced (space::xl);
    auto buttons = area.removeFromBottom (size::controlHeight);

    // Right-aligned with the widest grant outermost, which is where every other
    // dew dialog puts its acting button - and deny furthest from the thumb.
    //
    // The widths are measured rather than chosen: at 140 the primary button
    // read "Allow reading and changi", which is invisible in the code and
    // obvious in one dew_shot render.
    writeButton.setBounds (buttons.removeFromRight (150));
    buttons.removeFromRight (space::md);
    readButton.setBounds (buttons.removeFromRight (110));
    buttons.removeFromRight (space::md);
    denyButton.setBounds (buttons.removeFromRight (96));
}

ConsentHook consentWithPanel (juce::Component* parent)
{
    return [parent] (McpConsentPanel::Request request, std::function<void (control::Grant)> reply)
    { McpConsentPanel::show (std::move (request), parent, std::move (reply)); };
}

} // namespace dew
