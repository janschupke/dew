#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "app/ProjectDocument.h"
#include "ui/ChannelRackComponent.h"
#include "ui/EditorState.h"
#include "ui/design/Animator.h"
#include "ui/MixerComponent.h"
#include "ui/PianoRollComponent.h"
#include "ui/ParamContextMenu.h"
#include "ui/PlaylistComponent.h"
#include "ui/ScoreEditorComponent.h"
#include "ui/primitives/DewControls.h"

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

    /** Hands every editor that owns spec-built controls what their right-click
        menus need. Null means no menus. */
    void setParamMenuHost (const paramMenu::Host*);

    /** Where a preset row's description goes as the pointer passes over it.

        The status strip answers at once where the floating tooltip waits, which
        is the pair HoverHelp gives every other control - and a popup menu is a
        window of its own, so HoverHelp's own listener cannot reach these rows.
    */
    void setPresetHoverSink (std::function<void (const juce::String&)>);

    /** Where the arrangement is, so whoever makes an automation clip can show
        it rather than leaving it somewhere the user has to go and find. */
    static constexpr int playlistTabIndex = 2;

    /** The score tab, so the shell can route its status messages. */
    ScoreEditorComponent& getScoreEditor()
    {
        return scoreEditor;
    }

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

    /** The piano roll's pitch-row height, on the same "0 means leave it"
        contract as the lane height above. */
    int getPianoRollRowHeight() const;
    void setPianoRollRowHeight (int height);

    /** The piano roll's velocity lane height, on the same contract. */
    int getPianoRollVelocityHeight() const;
    void setPianoRollVelocityHeight (int height);

    /** The score tab's text size, as a rung index. */
    int getScoreFontStep() const;
    void setScoreFontStep (int step);

private:
    /** Every tab button this bar makes, refusing the right button.

        A stock juce::TabbedComponent hands out stock TabBarButtons, and
        TabBarButton::clicked re-reads the modifiers at RELEASE time: it opens a
        tab menu when they say popup and SWITCHES TAB when they do not. That is
        exactly how macOS spells a ctrl-click whose ctrl came up first, so a
        right-click on the tab bar changed editor. The rule the rest of the
        application follows is a latch made at the press - see PopupSafeButton.
    */
    juce::TabBarButton* createTabButton (const juce::String& tabName, int tabIndex) override;

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
