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
    setTabBarDepth (30);

    addTab ("Channel Rack", Palette::background, &channelRack, false);

    // The piano roll is taller than any window, so it gets a viewport.
    auto* scrollablePianoRoll = new juce::Viewport();
    scrollablePianoRoll->setViewedComponent (&pianoRoll, false);
    scrollablePianoRoll->setScrollBarsShown (true, false);
    addTab ("Piano Roll", Palette::background, scrollablePianoRoll, true);

    addTab ("Playlist", Palette::background, &playlist, false);
    addTab ("Mixer", Palette::background, &mixer, false);

    // Middle C is more useful to open on than the top of the range.
    scrollablePianoRoll->setViewPosition (0, 300);
}

void EditorTabs::refresh()
{
    channelRack.refresh();
    pianoRoll.refresh();
    playlist.refresh();
    mixer.refresh();
}

} // namespace dew
