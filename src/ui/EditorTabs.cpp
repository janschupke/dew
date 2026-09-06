#include "ui/EditorTabs.h"

#include "ui/design/DewLookAndFeel.h"

namespace dew
{

using namespace tokens;

EditorTabs::EditorTabs (ProjectDocument& document, AudioEngine& engine, EditorState& editorState,
                        SamplePool* pool)
    : juce::TabbedComponent (juce::TabbedButtonBar::TabsAtTop)
    , channelRack (document, engine, editorState, pool)
    , pianoRoll (document, engine, editorState)
    , playlist (document, engine, editorState, pool)
    , mixer (document, editorState, &engine)
    , scoreEditor (document)
{
    setComponentID ("editorTabs");

    // A group for one control: the panel fold chevron, which MainComponent
    // hands to setTabStripTrailing and which is therefore parented HERE. A
    // juce::TabbedComponent is not a container and each tab's content is one of
    // its own, so without this the chevron was the one control in the window
    // that belonged to no group at all. The tab buttons take no focus -
    // TabBarButton's constructor refuses it - so this group is the chevron and
    // nothing else.
    setTitle (tr (StringId::shell_editors));
    setFocusContainerType (FocusContainerType::focusContainer);
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

juce::TabBarButton* EditorTabs::createTabButton (const juce::String& tabName, int)
{
    return new PopupSafeButton<juce::TabBarButton> (tabName, getTabbedButtonBar());
}

void EditorTabs::setTabStripTrailing (juce::Component* c)
{
    tabStripTrailing = c;

    if (c != nullptr)
        addAndMakeVisible (*c);

    resized();
}

void EditorTabs::resized()
{
    juce::TabbedComponent::resized();

    if (tabStripTrailing == nullptr)
        return;

    // Taken OUT of the tab bar rather than laid on top of it: the bar is what
    // decides how much room the five tabs have, so a slot it does not know
    // about is a slot the last tab can grow into.
    auto bar = getTabbedButtonBar().getBounds();
    const auto slot = bar.removeFromRight (size::iconButton + space::sm);
    getTabbedButtonBar().setBounds (bar);

    tabStripTrailing->setBounds (slot.withSizeKeepingCentre (size::iconButton, size::iconButton));
}

void EditorTabs::paintOverChildren (juce::Graphics& g)
{
    const auto& bar = getTabbedButtonBar();

    if (bar.getRight() >= getWidth())
        return;

    // The strip's bottom hairline, continued across whatever the bar does not
    // cover.
    //
    // DewLookAndFeel::drawTabAreaBehindFrontButton draws that rule across the
    // TabbedButtonBar, and resized() above shrinks the bar by 32 pixels to
    // reserve the sidebar toggle's slot - so the rule stopped 32 pixels short
    // of the right edge and the strip had a bite out of it under the button.
    // Nothing else painted there: EditorTabs had no paint() at all and
    // MainComponent just fills the background.
    //
    // Measured from the BAR rather than from the slot resized() remembered: the
    // bar's own right edge is the thing the rule stops at, so asking it cannot
    // drift from whatever that width turns out to be.
    g.setColour (tokens::colour::dividerStrong);
    g.drawHorizontalLine (bar.getBottom() - 1, (float) bar.getRight(), (float) getWidth());
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

void EditorTabs::setPresetHoverSink (std::function<void (const juce::String&)> sink)
{
    mixer.setPresetHoverSink (std::move (sink));
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

int EditorTabs::getPianoRollRowHeight() const
{
    return pianoRoll.getRowHeight();
}

void EditorTabs::setPianoRollRowHeight (int height)
{
    if (height > 0)
        pianoRoll.setRowHeight (height);
}

int EditorTabs::getMixerEffectBandHeight() const
{
    return mixer.getEffectBandHeight();
}

void EditorTabs::setMixerEffectBandHeight (int pixels)
{
    if (pixels > 0)
        mixer.setEffectBandHeight (pixels);
}

int EditorTabs::getPianoRollVelocityHeight() const
{
    return pianoRoll.getVelocityHeight();
}

void EditorTabs::setPianoRollVelocityHeight (int height)
{
    if (height > 0)
        pianoRoll.setVelocityHeight (height);
}

int EditorTabs::getScoreFontStep() const
{
    return scoreEditor.getFontStep();
}

void EditorTabs::setScoreFontStep (int step)
{
    scoreEditor.setFontStep (step);
}

} // namespace dew
