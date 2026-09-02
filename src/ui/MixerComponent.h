#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../model/ProjectDocument.h"
#include "EditorState.h"

namespace dew
{

/** Mixer strips: a fader, a pan knob, mute and solo per insert, plus master. */
class MixerComponent : public juce::Component,
                       private juce::ValueTree::Listener
{
public:
    MixerComponent (ProjectDocument&, EditorState&);
    ~MixerComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void refresh();

private:
    class Strip;

    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;

    void rebuildStrips();

    static constexpr int stripWidth = 78;

    ProjectDocument& document;
    EditorState& editorState;
    juce::OwnedArray<Strip> strips;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerComponent)
};

} // namespace dew
