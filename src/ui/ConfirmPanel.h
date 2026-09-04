#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/primitives/DewControls.h"

namespace dew
{

/** "Are you sure?", asked the way dew asks anything else.

    dew had no confirmation of any kind. juce::AlertWindow appears in the tree
    only as colour and font overrides in DewLookAndFeel - styled, never opened -
    and NativeMessageBox not at all, which is right: a system dialog in an
    application whose whole design system exists so that nothing is system would
    be the one window that looked like somebody else's.

    So it is the same juce::Component-in-a-DialogWindow every other dew panel
    is, and inherits the dropdown, focus, escape and title-bar behaviour those
    already have through dialog::launch.

    It knows nothing about a document. What it confirms is a sentence and a
    callback, which is what lets the transport bar, the channel rack, the
    playlist and the mixer all use it while each keeps its own reason.
*/
class ConfirmPanel : public juce::Component
{
public:
    /** What is being asked.

        A struct rather than four parameters because the request TRAVELS: a
        component hands it to a hook, and that hook is the seam a test replaces
        to read what was asked without a dialog ever opening.
    */
    struct Request
    {
        juce::String title;                    ///< the window's title bar
        juce::String message;                  ///< one sentence, in the panel
        juce::String confirmText { "Delete" }; ///< what the acting button says
    };

    explicit ConfirmPanel (Request);

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Opens it, and runs `onConfirmed` only if the acting button is pressed.
        Cancel and escape close it and run nothing. */
    static void show (Request, juce::Component* parent, std::function<void()> onConfirmed);

    std::function<void()> onConfirm;

    // --- for tests -----------------------------------------------------------
    juce::Button& getConfirmButton() noexcept
    {
        return confirmButton;
    }
    juce::Button& getCancelButton() noexcept
    {
        return cancelButton;
    }
    const Request& getRequest() const noexcept
    {
        return request;
    }

    static constexpr int preferredWidth = 380;

    /** Room for three lines of the sentence and the button row under it. A
        deletion is named in one line and explained in a second; the third is
        headroom for a long name rather than a shape to fill. */
    static constexpr int preferredHeight = 128;

private:
    void closeDialog();

    Request request;

    DewButton cancelButton { "Cancel", DewButton::Role::ghost };

    /** Role::danger, always. Everything dew asks about is a deletion, and a
        confirmation whose acting button looks like an ordinary primary action
        is a confirmation that trains people to press it. */
    DewButton confirmButton { "Delete", DewButton::Role::danger };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConfirmPanel)
};

/** The seam every destructive action in dew goes through.

    A hook rather than a direct call to ConfirmPanel::show, for the same reason
    every menu here is a build/apply pair: JUCE_MODAL_LOOPS_PERMITTED is 0, so a
    DialogWindow is the one part of this that a headless test cannot drive. A
    test replaces the hook, asserts what was ASKED, and answers when it chooses.
*/
using ConfirmHook = std::function<void (ConfirmPanel::Request, std::function<void()> onConfirmed)>;

/** The default hook: opens the panel centred on `parent`.

    Assigned in a constructor rather than null-checked at each call site - a
    hook that has to be tested for is a hook somebody forgets to set, and there
    are four of these.
*/
ConfirmHook confirmWithPanel (juce::Component* parent);

} // namespace dew
