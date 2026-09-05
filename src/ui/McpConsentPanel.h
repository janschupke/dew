#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "control/McpProtocol.h"
#include "i18n/Strings.h"
#include "ui/DewDialog.h"
#include "ui/primitives/DewControls.h"

namespace dew
{

/** "May this program control dew?", asked of the person at the keyboard.

    The first thing dew asks that is not a deletion, which is why it is not a
    ConfirmPanel: that one hardcodes Role::danger and offers two buttons,
    because "everything dew asks about is a deletion" was true until now. This
    asks a question with THREE answers, and the middle one - read, but do not
    change - is the whole reason it is worth asking at all rather than having a
    switch in the settings.

    It names the client, and the name is the client's own claim about itself
    rather than anything dew can verify. That is exactly why a person is being
    asked: no check dew could perform would be worth as much as somebody
    recognising the thing they just started.

    Like every other dew dialog it is a dialog::Panel in a modeless DialogWindow
    through dialog::launch, because JUCE_MODAL_LOOPS_PERMITTED is 0 here - so
    the answer arrives in a callback and cannot arrive inline.
*/
class McpConsentPanel : public dialog::Panel
{
public:
    /** Who is asking. A struct because the request TRAVELS: the server hands it
        to a hook, and that hook is the seam a test replaces to read what was
        asked without a dialog ever opening. */
    struct Request
    {
        juce::String clientName;
        juce::String clientVersion;
    };

    explicit McpConsentPanel (Request);

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Opens it. `onAnswered` is called exactly once, with Grant::none if the
        window is closed or escaped - a dialog dismissed is a refusal, and
        leaving the caller waiting for an answer that never comes would hold a
        socket thread until it timed out. */
    static void show (Request, juce::Component* parent,
                      std::function<void (control::Grant)> onAnswered);

    std::function<void (control::Grant)> onAnswered;

    // --- for tests -----------------------------------------------------------
    juce::Button& getDenyButton() noexcept
    {
        return denyButton;
    }
    juce::Button& getReadButton() noexcept
    {
        return readButton;
    }
    juce::Button& getWriteButton() noexcept
    {
        return writeButton;
    }
    const Request& getRequest() const noexcept
    {
        return request;
    }

    static constexpr int preferredWidth = 460;

    /** Room for the question, four lines saying what a grant covers, and the
        button row. Wider and taller than ConfirmPanel because this asks
        somebody to make a decision rather than to confirm one they already
        made - and measured against a render rather than guessed: at 208 there
        was a band of empty ground between the sentence and the buttons.

        The WIDTH has to hold three buttons that measure their own words, so a
        language whose "Allow reading and changing" is much longer than the
        English needs this raised - and a dew_shot render is what says so. */
    static constexpr int preferredHeight = 186;

private:
    void answer (control::Grant);

    Request request;

    DewButton denyButton { tr (StringId::mcp_consent_deny), DewButton::Role::ghost };
    DewButton readButton { tr (StringId::mcp_consent_read), DewButton::Role::normal };
    DewButton writeButton { tr (StringId::mcp_consent_write), DewButton::Role::primary };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (McpConsentPanel)
};

/** The seam the server asks through.

    A hook rather than a call to McpConsentPanel::show, for the reason
    ConfirmHook is one: the dialog is the part of this a headless test cannot
    drive, so a test replaces the hook, asserts what was ASKED, and answers when
    it chooses.
*/
using ConsentHook = std::function<void (McpConsentPanel::Request,
                                        std::function<void (control::Grant)>)>;

ConsentHook consentWithPanel (juce::Component* parent);

} // namespace dew
