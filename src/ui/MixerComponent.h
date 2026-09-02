#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../engine/AudioEngine.h"
#include "../model/ProjectDocument.h"
#include "EditorState.h"
#include "EffectChainComponent.h"

namespace dew
{

/** Mixer strips: a fader, a pan knob, mute and solo per insert, plus master,
    and the effect chain of whichever strip is selected.

    The chain editor is shared rather than one per strip: a strip is 78px wide,
    which is nowhere near enough to edit a delay in, and a single editor also
    makes it obvious which chain you are looking at.
*/
class MixerComponent : public juce::Component,
                       private juce::ValueTree::Listener,
                       private juce::ChangeListener,
                       private juce::Timer
{
public:
    /** The engine is optional: it only supplies meter levels, and the mixer is
        constructed without one in tests and in the screenshot tool.
    */
    MixerComponent (ProjectDocument&, EditorState&, AudioEngine* = nullptr);
    ~MixerComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void refresh();

    /** The id the master strip answers to. Real inserts start at 1, so 0 can
        stand for "the master" without colliding with any of them.
    */
    static constexpr int masterTrackId = 0;

    /** Selects a channel and asks whoever owns the tabs to show the rack, so a
        routing entry in the mixer is a way to reach that channel.
    */
    std::function<void()> onShowChannelRack;

private:
    class Strip;

    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void rebuildStrips();
    void pointChainAtSelectedTrack();
    void layOutChain();
    void updateRouting();

    static constexpr int stripWidth = 96;
    static constexpr int chainHeight = 230;
    static constexpr int chainWidth = 430;

    ProjectDocument& document;
    EditorState& editorState;
    AudioEngine* engine = nullptr;
    juce::OwnedArray<Strip> strips;
    juce::Viewport stripViewport;
    juce::Component stripHolder;
    EffectChainComponent effectChain;
    juce::Viewport chainViewport;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerComponent)
};

} // namespace dew
