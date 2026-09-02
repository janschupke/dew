#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../engine/AudioEngine.h"
#include "../model/ProjectDocument.h"
#include "EditorState.h"
#include "StepGridComponent.h"

namespace dew
{

/** Channel headers on the left, the step grid on the right.

    Rows are rebuilt when channels are added or removed rather than being
    virtualised: a prototype's channel count is small, and the alternative buys
    complexity nothing here needs.
*/
class ChannelRackComponent : public juce::Component,
                             private juce::ValueTree::Listener,
                             private juce::ChangeListener
{
public:
    ChannelRackComponent (ProjectDocument&, AudioEngine&, EditorState&);
    ~ChannelRackComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void refresh();

private:
    class ChannelHeader;

    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void rebuildHeaders();

    static constexpr int headerWidth = 190;

    ProjectDocument& document;
    AudioEngine& engine;
    EditorState& editorState;

    juce::OwnedArray<ChannelHeader> headers;
    StepGridComponent grid;
    juce::Viewport viewport;
    juce::Component contentHolder;
    juce::TextButton addChannelButton { "+ Channel" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelRackComponent)
};

} // namespace dew
