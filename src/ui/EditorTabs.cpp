#include "ui/EditorTabs.h"

#include "ui/DewLookAndFeel.h"

namespace dew
{

EditorTabs::EditorTabs (ProjectDocument& document, AudioEngine& engine, EditorState& editorState,
                        SamplePool* pool)
    : juce::TabbedComponent (juce::TabbedButtonBar::TabsAtTop),
      channelRack (document, engine, editorState, pool),
      pianoRoll (document, engine, editorState),
      playlist (document, engine, editorState, pool),
      mixer (document, editorState, &engine),
      scoreEditor (document)
{
    setComponentID ("editorTabs");
    setTabBarDepth (tokens::size::stripTabs);

    // The alpha belongs to the editor, not to the whole tabbed component: the
    // tab bar must not fade with the thing it switched to.
    arrival.onChanged = [this]
    {
        if (auto* content = getCurrentContentComponent())
            content->setAlpha (arrival.get());
    };

    addTab ("Channel Rack", tokens::colour::background, &channelRack, false);

    // The piano roll used to live in a Viewport, which meant a ruler would have
    // scrolled away vertically and there was no way to scroll or zoom in time at
    // all. It scrolls itself now, in both directions, so the ruler, the keyboard
    // and the velocity lane stay pinned to the edges they belong to.
    addTab ("Piano Roll", tokens::colour::background, &pianoRoll, false);

    // Double-clicking a clip is the obvious way to go and edit its pattern.
    // The playlist does not know about tabs, so the wiring lives here.
    playlist.onOpenPatternInPianoRoll = [this] { setCurrentTabIndex (1); };

    // A routing entry in the mixer is a way to reach the channel it names.
    mixer.onShowChannelRack = [this] { setCurrentTabIndex (0); };

    addTab ("Playlist", tokens::colour::background, &playlist, false);
    addTab ("Mixer", tokens::colour::background, &mixer, false);

    // APPENDED, never inserted. The two setCurrentTabIndex calls above are
    // written as literals and Settings persists the raw index, so a tab added
    // anywhere but the end would silently reopen somebody on a different
    // editor than the one they left.
    addTab ("Score", tokens::colour::background, &scoreEditor, false);
}

void EditorTabs::compileScore()
{
    setCurrentTabIndex (getNumTabs() - 1);
    scoreEditor.compileIntoProject();
}

void EditorTabs::currentTabChanged (int newIndex, const juce::String& newName)
{
    juce::TabbedComponent::currentTabChanged (newIndex, newName);

    arrival.snapTo (0.0f);
    arrival.animateTo (1.0f, tokens::motion::panelMs, Ease::decelerate);
}

void EditorTabs::refresh()
{
    channelRack.refresh();
    pianoRoll.refresh();
    playlist.refresh();
    mixer.refresh();
    scoreEditor.refresh();
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

void EditorTabs::setParamMenuHost (const paramMenu::Host* host)
{
    channelRack.setParamMenuHost (host);
    mixer.setParamMenuHost (host);
}

int EditorTabs::getPlaylistTrackHeight() const
{
    return playlist.getTrackHeight();
}

void EditorTabs::setPlaylistTrackHeight (int height)
{
    if (height > 0)
        playlist.setTrackHeight (height);
}

} // namespace dew
