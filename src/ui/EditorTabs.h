#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "model/ProjectDocument.h"
#include "ui/ChannelRackComponent.h"
#include "ui/EditorState.h"
#include "ui/MixerComponent.h"
#include "ui/PianoRollComponent.h"
#include "ui/PlaylistComponent.h"

namespace dew
{

class SamplePool;

/** The four editors, one per tab: channel rack, piano roll, playlist, mixer. */
class EditorTabs : public juce::TabbedComponent
{
public:
    EditorTabs (ProjectDocument&, AudioEngine&, EditorState&, SamplePool* = nullptr);

    void refresh();

    /** The piano roll's zoom and scroll, for session persistence. */
    void capturePianoRollView (double& zoom, double& scroll, double& pitchScroll) const;
    void applyPianoRollView (double zoom, double scroll, double pitchScroll);

    /** The piano roll's snap division, as its ordinal. */
    int getPianoRollSnap() const;
    void setPianoRollSnap (int index);

private:
    ChannelRackComponent channelRack;
    PianoRollComponent pianoRoll;
    PlaylistComponent playlist;
    MixerComponent mixer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EditorTabs)
};

} // namespace dew
