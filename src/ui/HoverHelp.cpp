#include "ui/HoverHelp.h"

#include "ui/StatusBar.h"

namespace dew
{

HoverHelp::HoverHelp (juce::Component& r, StatusBar& s)
    : root (r)
    , statusBar (s)
{
    root.addMouseListener (this, true);
}

HoverHelp::~HoverHelp()
{
    root.removeMouseListener (this);
}

juce::String HoverHelp::helpFor (juce::Component& component)
{
    for (auto* c = &component; c != nullptr; c = c->getParentComponent())
        if (auto* client = dynamic_cast<juce::TooltipClient*> (c))
            if (const auto tip = client->getTooltip(); tip.isNotEmpty())
                return tip;

    return {};
}

void HoverHelp::report (const juce::MouseEvent& event)
{
    statusBar.setHoverHelp (event.eventComponent != nullptr ? helpFor (*event.eventComponent)
                                                            : juce::String());
}

void HoverHelp::mouseMove (const juce::MouseEvent& event)
{
    report (event);
}
void HoverHelp::mouseEnter (const juce::MouseEvent& event)
{
    report (event);
}

// A press changes what is under the pointer often enough to matter - a tab, a
// row that rebuilds - and the strip would otherwise keep describing whatever
// was there before the click.
void HoverHelp::mouseDown (const juce::MouseEvent& event)
{
    report (event);
}

void HoverHelp::mouseExit (const juce::MouseEvent& event)
{
    // Not simply cleared: leaving a knob for the panel it sits on is an exit,
    // and blanking the strip there would make the line flicker every time the
    // pointer crossed a control's edge. Whatever is under the pointer NOW is
    // the honest answer, and outside everything that is nothing.
    report (event);
}

} // namespace dew
