#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "app/ProjectDocument.h"
#include "ui/EditorState.h"
#include "ui/ParamContextMenu.h"
#include "ui/EffectChainHost.h"

namespace dew
{

/** Two rows. Along the top, one strip per insert - a fader, a pan knob, mute
    and solo - plus master; along the bottom, the effect chain of whichever
    strip is selected, its effects side by side.

    The chain editor is shared rather than one per strip: a strip is 96px wide,
    which is nowhere near enough to edit a delay in, and a single editor also
    makes it obvious which chain you are looking at.

    It runs across rather than down because that is the shape the space has. A
    column of cards in the bottom of a mixer is a narrow slot with a thousand
    pixels of empty panel beside it, and it was one - the chain used to be
    capped at 430px wide and scrolled inside a 230px porthole.

    Both rows scroll sideways independently, so both carry a scrollbar.
*/
class MixerComponent : public juce::Component,
                       private juce::ValueTree::Listener,
                       private juce::ChangeListener,
                       private juce::Timer
{
public:
    /** Set from above; null means no automation menus. */
    void setParamMenuHost (const paramMenu::Host* host);

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
    void updateRouting();

    static constexpr int stripWidth = 96;

    const paramMenu::Host* paramMenuHost = nullptr;

    ProjectDocument& document;
    EditorState& editorState;
    AudioEngine* engine = nullptr;
    juce::OwnedArray<Strip> strips;
    juce::Viewport stripViewport;
    juce::Component stripHolder;
    EffectChainHost chainHost;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerComponent)
};

} // namespace dew
