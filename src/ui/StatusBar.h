#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/design/Animator.h"

#include "io/LiveAudioHost.h"
#include "model/ProjectDocument.h"
#include "ui/EditorState.h"

namespace dew
{

/** The strip along the bottom: what you are looking at, what just happened, and
    what the engine is coping with.

    Status used to be a juce::Label crammed into the right end of the transport
    bar, written by three call sites with no priority and no timeout, so a load
    warning and the device description overwrote each other and whichever came
    last stayed forever.
*/
class StatusBar : public juce::Component,
                  private juce::Timer,
                  private juce::ChangeListener,
                  private juce::ValueTree::Listener
{
public:
    enum class Severity { info, success, warning, error };

    StatusBar (ProjectDocument&, EditorState&, LiveAudioHost&);
    ~StatusBar() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Shows a message for a few seconds. A more severe message is not replaced
        by a less severe one while it is still on screen, so an error cannot be
        buried by a routine "saved" a moment later.
    */
    void showMessage (const juce::String&, Severity = Severity::info);

    /** What the control under the pointer is for.

        Shown at once, where the floating tooltip waits 600ms - so the strip
        answers "what is this?" while you are still moving, and the tooltip is
        there for anyone who stops. One help string, two surfaces: both read the
        control's own TooltipClient text, so a control cannot explain itself in
        one place and not the other.

        Yields to a transient message. An error and "what that button does" are
        not comparable in importance, and the message already has a severity
        rule saying so.
    */
    void setHoverHelp (const juce::String&);

    juce::String getHoverHelp() const { return hoverText; }

    void refresh();

    // --- for tests -----------------------------------------------------------
    juce::String getMessageText() const  { return messageText; }
    Severity getMessageSeverity() const  { return messageSeverity; }
    juce::String getContextText() const  { return contextText; }
    bool hasMessage() const;

    /** Advances the message clock without waiting, so expiry is testable. */
    void advanceMessageClock (int milliseconds);

    static constexpr int messageLifetimeMs = 6000;

private:
    /** How far a newly-arrived message has faded in. The fade OUT was already
        here and is driven by the message's own age, which is why the two are
        separate numbers rather than one. */
    ComponentMotion arrival { *this, 1.0f };

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;

    void updateContext();
    juce::Colour colourFor (Severity) const;

    ProjectDocument& document;
    EditorState& editorState;
    LiveAudioHost& audioHost;

    juce::String contextText;
    juce::String messageText;
    juce::String hoverText;
    Severity messageSeverity = Severity::info;
    int messageAgeMs = 0;

    double dspLoad = 0.0;
    int dropouts = 0;
    int lastDropouts = 0;
    int dropoutFlashMs = 0;

    juce::Rectangle<int> contextBounds, messageBounds, loadBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StatusBar)
};

} // namespace dew
