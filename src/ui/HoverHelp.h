#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace dew
{

class StatusBar;

/** Says what the control under the pointer is for, in the status bar.

    dew had thirty-odd setTooltip calls and a floating tooltip window, and that
    is a good answer for someone who has stopped moving and is asking about ONE
    control. It is a bad answer for the question the complaint was actually
    about - "what are all these?" - because reading eight of them means hovering
    eight times and waiting 600ms each time.

    So the same string goes to the strip along the bottom, at once. One help
    string on two surfaces, both read from the control's own TooltipClient, so a
    control cannot explain itself in one place and not the other and there is no
    second vocabulary to keep in step.

    ONE listener on the top-level component rather than a hook on every control.
    Component::addMouseListener takes wantsEventsForAllNestedChildComponents,
    which is what makes this a class rather than a convention nobody follows.

    It walks UP from the component the event landed on, because the thing with
    the tooltip is not always the thing the pointer is over: a DewKnob puts its
    tooltip on the juce::Slider inside it, and a Label inside a header has none
    of its own but sits on a row that does.
*/
class HoverHelp : private juce::MouseListener
{
public:
    /** Listens to `root` and everything inside it. Both must outlive this. */
    HoverHelp (juce::Component& root, StatusBar&);
    ~HoverHelp() override;

    /** What the strip would say for this component: its own tooltip, or the
        nearest one above it, or nothing.

        The test seam. Mouse events reach a component through a ComponentPeer,
        which a headless harness has none of, so a test asks this directly - and
        the coverage gate asks it of every control in the window.
    */
    static juce::String helpFor (juce::Component&);

private:
    void mouseMove (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;

    void report (const juce::MouseEvent&);

    juce::Component& root;
    StatusBar& statusBar;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HoverHelp)
};

} // namespace dew
