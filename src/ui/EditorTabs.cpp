#include "EditorTabs.h"

#include "DewLookAndFeel.h"

namespace dew
{

EditorTabs::EditorTabs (ProjectDocument& document, AudioEngine& engine, EditorState& editorState)
    : juce::TabbedComponent (juce::TabbedButtonBar::TabsAtTop),
      channelRack (document, engine, editorState),
      pianoRoll (document, engine, editorState),
      playlist (document, engine, editorState),
      mixer (document, editorState, &engine)
{
    setComponentID ("editorTabs");
    setTabBarDepth (30);

    addTab ("Channel Rack", Palette::background, &channelRack, false);

    // The piano roll used to live in a Viewport, which meant a ruler would have
    // scrolled away vertically and there was no way to scroll or zoom in time at
    // all. It scrolls itself now, in both directions, so the ruler, the keyboard
    // and the velocity lane stay pinned to the edges they belong to.
    addTab ("Piano Roll", Palette::background, &pianoRoll, false);

    // Double-clicking a clip is the obvious way to go and edit its pattern.
    // The playlist does not know about tabs, so the wiring lives here.
    playlist.onOpenPatternInPianoRoll = [this] { setCurrentTabIndex (1); };

    // A routing entry in the mixer is a way to reach the channel it names.
    mixer.onShowChannelRack = [this] { setCurrentTabIndex (0); };

    addTab ("Playlist", Palette::background, &playlist, false);
    addTab ("Mixer", Palette::background, &mixer, false);
}

void EditorTabs::refresh()
{
    channelRack.refresh();
    pianoRoll.refresh();
    playlist.refresh();
    mixer.refresh();
}

void EditorTabs::capturePianoRollView (double& zoom, double& scroll, double& pitchScroll) const
{
    pianoRoll.captureView (zoom, scroll, pitchScroll);
}

void EditorTabs::applyPianoRollView (double zoom, double scroll, double pitchScroll)
{
    pianoRoll.applyView (zoom, scroll, pitchScroll);
}

int EditorTabs::getPianoRollSnap() const
{
    return NoteTools::indexOfSnap (pianoRoll.getSnap());
}

void EditorTabs::setPianoRollSnap (int index)
{
    pianoRoll.setSnap (NoteTools::snapFromIndex (index));
}

} // namespace dew
