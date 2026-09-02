#include "EditorTabs.h"

#include "DewLookAndFeel.h"

namespace dew
{

EditorTabs::EditorTabs (ProjectDocument& document, AudioEngine& engine, EditorState& editorState)
    : juce::TabbedComponent (juce::TabbedButtonBar::TabsAtTop),
      channelRack (document, engine, editorState),
      pianoRoll (document, engine, editorState),
      playlist (document, engine, editorState),
      mixer (document, editorState)
{
    setComponentID ("editorTabs");
    setTabBarDepth (30);

    addTab ("Channel Rack", Palette::background, &channelRack, false);

    // The piano roll used to live in a Viewport, which meant a ruler would have
    // scrolled away vertically and there was no way to scroll or zoom in time at
    // all. It scrolls itself now, in both directions, so the ruler, the keyboard
    // and the velocity lane stay pinned to the edges they belong to.
    addTab ("Piano Roll", Palette::background, &pianoRoll, false);

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

} // namespace dew
