#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "i18n/Strings.h"

#include "engine/AudioEngine.h"
#include "app/ProjectDocument.h"
#include "ui/ConfirmPanel.h"
#include "ui/ChannelRackHeader.h"
#include "ui/EditorState.h"
#include "ui/ParamContextMenu.h"
#include "ui/TimelineRuler.h"
#include "ui/StepGridComponent.h"
#include "ui/ZoomButtons.h"
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
    /** Set from above; null means no automation menus. */
    void setParamMenuHost (const paramMenu::Host* host);

    ChannelRackComponent (ProjectDocument&, AudioEngine&, EditorState&, SamplePool* = nullptr);
    ~ChannelRackComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void refresh();

    void addChannel();

    /** Adds the kind the menu named, by asking whichever of the three below
        knows how. The rack is the only thing that can make all three. */
    void addChannelOfType (InstrumentType);

    /** Adds a channel that plays a recording rather than its oscillators, and
        arms it - the reason to add one is to record into it.
    */
    void addAudioChannel();
    void addSoundFontChannel();
    void removeChannel (int channelId);

    /** What removing a channel asks first. See ConfirmHook. */
    ConfirmHook confirmDestructive;

    /** Drives a row's context-menu item without opening the menu.

        juce::PopupMenu::showMenuAsync cannot run headlessly, so the menu is the
        only part of this that a test cannot reach. Everything it does is behind
        here instead, and returns false if there is no such row.
    */
    bool applyChannelMenuChoice (int channelId, int choice);

    /** The items a row's menu offers, for a test to assert against. */
    juce::StringArray channelMenuItems (int channelId) const;

private:
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void rebuildHeaders();

    /** Where a control's right-click gets what a row does not know: the
        playhead, and how to show the clip it makes. */
    const paramMenu::Host* paramMenuHost = nullptr;

    ProjectDocument& document;
    AudioEngine& engine;
    EditorState& editorState;

    juce::OwnedArray<ChannelRackHeader> headers;
    StepGridComponent grid;

    /** The rack's ruler spans the step columns, which leaves its header-column
        corner empty. That corner is exactly where a zoom control belongs: it is
        beside the ruler it changes, and it was dead space. */
    ZoomButtons zoomButtons { "Fit the pattern to the window (0)" };

    // Above the viewport, not inside it. The grid scrolls vertically, so a
    // ruler drawn as part of the grid would scroll away with the channels.
    RulerStrip ruler;
    juce::Viewport viewport;
    juce::Component contentHolder;
    // In the header column, below the last channel, and inside the scrolling
    // holder - so it is the next empty ROW of the list rather than an action
    // parked in a footer strip at the far end of the panel.
    DewButton addChannelButton { tr (StringId::channelRack_addChannel_label),
                                 DewButton::Role::ghost };
    DewButton addAudioButton { tr (StringId::channelRack_addAudio_label), DewButton::Role::ghost };
    DewButton addSoundFontButton { tr (StringId::channelRack_addSoundFont_label),
                                   DewButton::Role::ghost };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelRackComponent)
};

} // namespace dew
