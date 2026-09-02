#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

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
                       private juce::ChangeListener
{
public:
    MixerComponent (ProjectDocument&, EditorState&);
    ~MixerComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void refresh();

    /** The id the master strip answers to. Real inserts start at 1, so 0 can
        stand for "the master" without colliding with any of them.
    */
    static constexpr int masterTrackId = 0;

private:
    class Strip;

    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void rebuildStrips();
    void pointChainAtSelectedTrack();
    void layOutChain();

    static constexpr int stripWidth = 78;
    static constexpr int chainHeight = 210;
    static constexpr int chainWidth = 430;

    ProjectDocument& document;
    EditorState& editorState;
    juce::OwnedArray<Strip> strips;
    EffectChainComponent effectChain;
    juce::Viewport chainViewport;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerComponent)
};

} // namespace dew
