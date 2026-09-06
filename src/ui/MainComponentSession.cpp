// =============================================================================
// MainComponent - the session: what the window is, as opposed to what the
// project contains.
//
// The same class, a fourth translation unit beside MainComponentJobs.cpp and
// MainComponentControl.cpp, and split out for the reason they were:
// MainComponent.cpp had reached the four hundred code lines the tree allows a
// file, and it was one line under before anything was added to it.
//
// One subject, and the seam was already there. applySettings and
// captureSettings are the two halves of the same sentence - what is restored at
// launch and what is written at exit - and every value they carry is the
// WINDOW's rather than the document's: a panel width, a tab index, a zoom, a
// remembered selection. Keyboard input mode is here for exactly that reason
// too. It belongs to no editor and to no panel: it follows the keyboard focus
// across the whole tab strip, plays whichever channel is selected, and has to
// be reachable from a menu command, a button on the transport bar and a
// restored session at once. Nothing else in the class reads TypingKeyboard.
// =============================================================================

#include "ui/MainComponent.h"

#include "i18n/Strings.h"
#include "ui/PianoRollNotes.h"
#include "ui/design/Animator.h"
#include "ui/design/SystemMotionPreference.h"

namespace dew
{

void MainComponent::wireKeyboardInput()
{
    transportBar.onToggleKeyboardInput = [this]
    { typingKeyboard.setEnabled (! typingKeyboard.isEnabled()); };

    transportBar.isKeyboardInputEnabled = [this] { return typingKeyboard.isEnabled(); };

    // Listens to THIS for good AND to whatever holds the keyboard as it moves.
    // Both, because a listener on the window beats the command manager's
    // mapping set but not a focused editor's own keyPressed - see
    // TypingKeyboard, which spends a page on why that is the shape.
    typingKeyboard.followFocus (*this);

    // The octave, said out loud. The map moves two and a half octaves and the
    // keys do not change under the fingers, so without this the only way to
    // know where it is standing is to play a note and listen.
    typingKeyboard.onOctaveChanged = [this] (int octave)
    {
        statusBar.showMessage (tr (StringId::status_keyboardOctave,
                                   Args {}.with ("note", pianoRoll::noteName (12 * (octave + 1)))),
                               StatusBar::Severity::info);
    };
}

void MainComponent::applySettings (const Settings& settings)
{
    panelWidth = settings.getPanelWidth();
    panelCollapsed = settings.getPanelCollapsed();
    updatePanelToggle();

    Animator::shared().setReduceMotion (settings.getReduceMotion (systemPrefersReducedMotion()));

    editorState.setSelectedChannelId (settings.getSelectedChannelId());
    editorState.setSelectedMixerTrackId (settings.getSelectedMixerTrackId());

    // Answered against the document, the way the pattern id below already is.
    resolveSelectedChannel();

    // Through the transport bar rather than straight into EditorState, because
    // a remembered pattern id is a claim about a project the settings file has
    // never seen: Settings::getCurrentPatternId only clamps it to one or more.
    // The bar answers it against the document, falls back to the first pattern
    // there is, and tells the engine - none of which the raw setter does.
    transportBar.setCurrentPattern (settings.getCurrentPatternId());

    tabs.setCurrentTabIndex (settings.getTabIndex(), false);
    tabs.applyPianoRollView (settings.getPianoRollZoom(), settings.getPianoRollScroll(),
                             settings.getPianoRollPitchScroll());
    tabs.setPianoRollSnap (settings.getPianoRollSnap());
    tabs.setPlaylistTrackHeight (settings.getPlaylistTrackHeight());
    tabs.setPianoRollRowHeight (settings.getPianoRollRowHeight());
    tabs.setPianoRollVelocityHeight (settings.getPianoRollVelocityHeight());
    tabs.setMixerEffectBandHeight (settings.getMixerEffectBandHeight());
    tabs.setScoreFontStep (settings.getScoreFontStep());

    // Which devices are enabled rides the audio device XML, restored by the
    // app; only these two are dew's own.
    midiHost.getRouter().setChannelFilter (settings.getMidiChannelFilter());
    midiHost.getRouter().setTranspose (settings.getMidiTranspose());

    transportBar.applyMetronomeSettings (settings);
    typingKeyboard.setOctave (settings.getKeyboardInputOctave());
    typingKeyboard.setEnabled (settings.getKeyboardInputEnabled());

    resized();
}

void MainComponent::captureSettings (Settings& settings) const
{
    settings.setPanelWidth (panelWidth);
    settings.setPanelCollapsed (panelCollapsed);
    settings.setTabIndex (tabs.getCurrentTabIndex());
    settings.setSelectedChannelId (editorState.getSelectedChannelId());
    settings.setSelectedMixerTrackId (editorState.getSelectedMixerTrackId());
    settings.setCurrentPatternId (editorState.getCurrentPatternId());

    double zoom = 0.0, scroll = 0.0, pitch = 0.0;
    tabs.capturePianoRollView (zoom, scroll, pitch);

    settings.setMidiChannelFilter (midiHost.getRouter().getChannelFilter());
    settings.setMidiTranspose (midiHost.getRouter().getTranspose());

    settings.setPianoRollZoom (zoom);
    settings.setPianoRollScroll (scroll);
    settings.setPianoRollPitchScroll (pitch);
    settings.setPianoRollSnap (tabs.getPianoRollSnap());
    settings.setPlaylistTrackHeight (tabs.getPlaylistTrackHeight());
    settings.setPianoRollRowHeight (tabs.getPianoRollRowHeight());
    settings.setPianoRollVelocityHeight (tabs.getPianoRollVelocityHeight());
    settings.setMixerEffectBandHeight (tabs.getMixerEffectBandHeight());
    settings.setScoreFontStep (tabs.getScoreFontStep());

    transportBar.captureMetronomeSettings (settings);
    settings.setKeyboardInputEnabled (typingKeyboard.isEnabled());
    settings.setKeyboardInputOctave (typingKeyboard.getOctave());
}

} // namespace dew
