#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../engine/AudioEngine.h"
#include "../model/ProjectDocument.h"
#include "ChannelRackComponent.h"
#include "EditorState.h"
#include "MixerComponent.h"
#include "PianoRollComponent.h"
#include "PlaylistComponent.h"

namespace dew
{

/** The four editors, one per tab: channel rack, piano roll, playlist, mixer. */
class EditorTabs : public juce::TabbedComponent
{
public:
    EditorTabs (ProjectDocument&, AudioEngine&, EditorState&);

    void refresh();

private:
    ChannelRackComponent channelRack;
    PianoRollComponent pianoRoll;
    PlaylistComponent playlist;
    MixerComponent mixer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EditorTabs)
};

} // namespace dew
