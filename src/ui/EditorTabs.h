#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "model/ProjectDocument.h"
#include "ui/ChannelRackComponent.h"
#include "ui/EditorState.h"
#include "ui/design/Animator.h"
#include "ui/MixerComponent.h"
#include "ui/PianoRollComponent.h"
#include "ui/PlaylistComponent.h"
#include "ui/ScoreEditorComponent.h"

namespace dew
{

class SamplePool;

/** The five editors, one per tab: channel rack, piano roll, playlist, mixer,
    and the score - the language, edited in the application it compiles for.
*/
class EditorTabs : public juce::TabbedComponent
{
public:
    EditorTabs (ProjectDocument&, AudioEngine&, EditorState&, SamplePool* = nullptr);

    void refresh();

    /** The score tab, so the shell can route its status messages. */
    ScoreEditorComponent& getScoreEditor() { return scoreEditor; }

    /** Shows the score tab and compiles it. What Command-R does. */
    void compileScore();

    /** The piano roll's zoom and scroll, for session persistence. */
    void capturePianoRollView (double& zoom, double& scroll, double& pitchScroll) const;
    void applyPianoRollView (double zoom, double scroll, double pitchScroll);

    /** The piano roll's snap division, as its ordinal. */
    int getPianoRollSnap() const;
    void setPianoRollSnap (int index);

    /** The playlist's lane height. 0 on the way in means "leave the default",
        which is what a settings file that predates the control stores. */
    int getPlaylistTrackHeight() const;
    void setPlaylistTrackHeight (int height);

private:
    /** The incoming editor's alpha, 0 to 1 over panelMs.

        Deliberately NOT a crossfade. Keeping the outgoing editor visible means
        two live editors, two sixty-hertz playhead timers and visibly two
        playheads; the incoming one fades up over the window background
        instead, which is what "switched tab" actually looks like.
    */
    void currentTabChanged (int newIndex, const juce::String& newName) override;

    ComponentMotion arrival { *this, 1.0f };

    ChannelRackComponent channelRack;
    PianoRollComponent pianoRoll;
    PlaylistComponent playlist;
    MixerComponent mixer;
    ScoreEditorComponent scoreEditor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EditorTabs)
};

} // namespace dew
