#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "model/ProjectDocument.h"
#include "ui/EditorState.h"
#include "ui/TimelineRuler.h"
#include "ui/StepGridComponent.h"
#include "ui/primitives/DewControls.h"

namespace dew
{

class SamplePool;

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
    ChannelRackComponent (ProjectDocument&, AudioEngine&, EditorState&, SamplePool* = nullptr);
    ~ChannelRackComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void refresh();

    void addChannel();

    /** Adds a channel that plays a recording rather than its oscillators, and
        arms it - the reason to add one is to record into it.
    */
    void addAudioChannel();
    void removeChannel (int channelId);

    /** Drives a row's context-menu item without opening the menu.

        juce::PopupMenu::showMenuAsync cannot run headlessly, so the menu is the
        only part of this that a test cannot reach. Everything it does is behind
        here instead, and returns false if there is no such row.
    */
    bool applyChannelMenuChoice (int channelId, int choice);

    /** The items a row's menu offers, for a test to assert against. */
    juce::StringArray channelMenuItems (int channelId) const;

private:
    class ChannelHeader;

    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void rebuildHeaders();

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
    // In the header column, below the last channel, and inside the scrolling
    // holder - so it is the next empty ROW of the list rather than an action
    // parked in a footer strip at the far end of the panel.
    DewButton addChannelButton { "+ Channel", DewButton::Role::ghost };
    DewButton addAudioButton { "+ Audio", DewButton::Role::ghost };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelRackComponent)
};

} // namespace dew
