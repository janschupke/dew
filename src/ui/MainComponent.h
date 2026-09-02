#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

namespace dew
{

/** Phase 1 placeholder: proves the window, the static-library link and the
    dependency pin all reach the screen. Replaced by the real editor shell
    (transport bar + channel rack / piano roll / playlist / mixer) in phase 4.
*/
class MainComponent : public juce::Component
{
public:
    MainComponent();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Label titleLabel;
    juce::Label buildLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace dew
