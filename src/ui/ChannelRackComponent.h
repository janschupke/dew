#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../engine/AudioEngine.h"
#include "../model/ProjectDocument.h"
#include "EditorState.h"
#include "TimelineRuler.h"
#include "StepGridComponent.h"
#include "primitives/DewControls.h"

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

    static constexpr int footerHeight = 40;

    ProjectDocument& document;
    AudioEngine& engine;
    EditorState& editorState;

    juce::OwnedArray<ChannelHeader> headers;
    StepGridComponent grid;

    // Above the viewport, not inside it. The grid scrolls vertically, so a
    // ruler drawn as part of the grid would scroll away with the channels.
    RulerStrip ruler;
    juce::Viewport viewport;
    juce::Component contentHolder;
    DewButton addChannelButton { "+ Channel", DewButton::Role::primary };
    DewButton removeChannelButton { "Remove", DewButton::Role::ghost };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelRackComponent)
};

} // namespace dew
