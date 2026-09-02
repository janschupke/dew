#include "PlaylistComponent.h"

#include "../model/Ids.h"
#include "../model/ProjectEdits.h"
#include "DewLookAndFeel.h"

namespace dew
{

PlaylistComponent::PlaylistComponent (ProjectDocument& d, AudioEngine& e, EditorState& s)
    : document (d), engine (e), editorState (s)
{
    setComponentID ("playlist");
    document.getState().addListener (this);
    editorState.addChangeListener (this);
    startTimerHz (30);
}

PlaylistComponent::~PlaylistComponent()
{
    document.getState().removeListener (this);
    editorState.removeChangeListener (this);
}

void PlaylistComponent::refresh()
{
    document.getState().addListener (this);
    repaint();
}

juce::ValueTree PlaylistComponent::playlist() const
{
    return document.getState().getChildWithName (ids::PLAYLIST);
}

int PlaylistComponent::numBars() const
{
    return juce::jmax (4, (int) document.getState()[ids::barsInSong]);
}

float PlaylistComponent::barWidth() const
{
    return (float) juce::jmax (1, getWidth() - headerWidth) / (float) numBars();
}

int PlaylistComponent::barAtX (int x) const
{
    return juce::jlimit (0, numBars() - 1, (int) ((float) (x - headerWidth) / barWidth()));
}

int PlaylistComponent::trackAtY (int y) const
{
    return (y - rulerHeight) / rowHeight;
}

juce::ValueTree PlaylistComponent::trackAt (int index) const
{
    int i = 0;

    for (const auto& track : playlist())
        if (track.hasType (ids::PLAYLIST_TRACK) && i++ == index)
            return track;

    return {};
}

juce::Rectangle<float> PlaylistComponent::boundsForClip (const juce::ValueTree& clip, int trackIndex) const
{
    const auto start = (int) clip[ids::startBar];
    const auto length = juce::jmax (1, (int) clip[ids::lengthBars]);

    return { (float) headerWidth + (float) start * barWidth(),
             (float) (rulerHeight + trackIndex * rowHeight),
             (float) length * barWidth(),
             (float) rowHeight };
}

bool PlaylistComponent::isOnRightEdge (const juce::ValueTree& clip, int trackIndex,
                                       juce::Point<int> position) const
{
    const auto bounds = boundsForClip (clip, trackIndex);
    const auto edge = juce::jmin (10.0f, bounds.getWidth() * 0.3f);

    return (float) position.x >= bounds.getRight() - edge;
}

void PlaylistComponent::mouseMove (const juce::MouseEvent& event)
{
    const auto trackIndex = trackAtY (event.y);
    const auto track = trackAt (trackIndex);
    const auto clip = track.isValid() ? ProjectEdits::findClipAtBar (track, barAtX (event.x))
                                      : juce::ValueTree();

    setMouseCursor (clip.isValid() && isOnRightEdge (clip, trackIndex, event.getPosition())
                        ? juce::MouseCursor::LeftRightResizeCursor
                        : juce::MouseCursor::NormalCursor);
}

void PlaylistComponent::mouseDown (const juce::MouseEvent& event)
{
    if (event.x < headerWidth || event.y < rulerHeight)
        return;

    const auto trackIndex = trackAtY (event.y);
    auto track = trackAt (trackIndex);

    if (! track.isValid())
        return;

    const auto bar = barAtX (event.x);
    auto clip = ProjectEdits::findClipAtBar (track, bar);
    auto& undo = document.getUndoManager();

    if (event.mods.isPopupMenu() || event.mods.isAltDown())
    {
        if (clip.isValid())
        {
            undo.beginNewTransaction ("Delete clip");
            ProjectEdits::removeClip (track, clip, &undo);
            repaint();
        }
        return;
    }

    if (clip.isValid())
    {
        draggedClip = clip;
        draggedClipTrack = track;

        if (isOnRightEdge (clip, trackIndex, event.getPosition()))
        {
            gesture = Gesture::resizing;
            undo.beginNewTransaction ("Resize clip");
        }
        else
        {
            gesture = Gesture::moving;
            dragBarOffset = bar - (int) clip[ids::startBar];
            undo.beginNewTransaction ("Move clip");
        }

        return;
    }

    undo.beginNewTransaction ("Add clip");
    draggedClip = ProjectEdits::addClip (track, editorState.getCurrentPatternId(), bar, 1, &undo);
    draggedClipTrack = track;
    gesture = Gesture::resizing;
    repaint();
}

void PlaylistComponent::mouseDrag (const juce::MouseEvent& event)
{
    if (! draggedClip.isValid())
        return;

    auto& undo = document.getUndoManager();

    if (gesture == Gesture::resizing)
    {
        const auto length = barAtX (event.x) - (int) draggedClip[ids::startBar] + 1;
        ProjectEdits::resizeClip (draggedClip, juce::jmax (1, length), &undo);
    }
    else if (gesture == Gesture::moving)
    {
        ProjectEdits::moveClip (draggedClip,
                                juce::jlimit (0, numBars() - 1, barAtX (event.x) - dragBarOffset),
                                &undo);
    }

    repaint();
}

void PlaylistComponent::mouseUp (const juce::MouseEvent&)
{
    draggedClip = {};
    draggedClipTrack = {};
    gesture = Gesture::none;
}

void PlaylistComponent::timerCallback()
{
    const auto stepsPerBar = juce::jmax (1, (int) document.getState()[ids::stepsPerBeat]) * 4;
    const auto bar = (int) engine.getPlayheadSteps() / stepsPerBar;

    if (bar != lastPlayheadBar)
    {
        lastPlayheadBar = bar;
        repaint();
    }
}

void PlaylistComponent::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) { repaint(); }
void PlaylistComponent::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&)             { repaint(); }
void PlaylistComponent::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int)      { repaint(); }
void PlaylistComponent::changeListenerCallback (juce::ChangeBroadcaster*)                    { repaint(); }

void PlaylistComponent::paint (juce::Graphics& g)
{
    g.fillAll (Palette::panelDark);

    const auto bars = numBars();
    const auto width = barWidth();

    // --- ruler ---------------------------------------------------------------
    g.setColour (Palette::panel);
    g.fillRect (0, 0, getWidth(), rulerHeight);

    g.setFont (juce::FontOptions (10.0f));

    for (int bar = 0; bar < bars; ++bar)
    {
        const auto x = (float) headerWidth + (float) bar * width;

        g.setColour (Palette::textDim);
        g.drawText (juce::String (bar + 1), (int) x + 3, 0, (int) width - 4, rulerHeight,
                    juce::Justification::centredLeft);

        g.setColour (Palette::lineStrong);
        g.drawVerticalLine ((int) x, 0.0f, (float) getHeight());
    }

    // --- tracks --------------------------------------------------------------
    int trackIndex = 0;

    for (const auto& track : playlist())
    {
        if (! track.hasType (ids::PLAYLIST_TRACK))
            continue;

        const auto y = rulerHeight + trackIndex * rowHeight;

        g.setColour (Palette::panel);
        g.fillRect (0, y, headerWidth, rowHeight);

        g.setColour (Palette::text);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (track[ids::name].toString(), 8, y, headerWidth - 12, rowHeight,
                    juce::Justification::centredLeft);

        g.setColour (Palette::line);
        g.drawHorizontalLine (y, 0.0f, (float) getWidth());

        for (const auto& clip : track)
        {
            if (! clip.hasType (ids::CLIP))
                continue;

            const auto bounds = boundsForClip (clip, trackIndex).reduced (2.0f, 3.0f);
            const auto pattern = ProjectEdits::findPattern (document.getState(),
                                                            (int) clip[ids::patternId]);

            const auto isCurrent = (int) clip[ids::patternId] == editorState.getCurrentPatternId();
            const auto colour = isCurrent ? Palette::accent : Palette::accent.withSaturation (0.35f);

            g.setColour (colour.withAlpha (0.75f));
            g.fillRoundedRectangle (bounds, 3.0f);
            g.setColour (colour.brighter (0.3f));
            g.drawRoundedRectangle (bounds, 3.0f, 1.0f);

            g.setColour (Palette::background);
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (pattern.isValid() ? pattern[ids::name].toString()
                                          : "pattern " + clip[ids::patternId].toString(),
                        bounds.toNearestInt().reduced (5, 0), juce::Justification::centredLeft, true);
        }

        ++trackIndex;
    }

    g.setColour (Palette::line);
    g.drawVerticalLine (headerWidth, 0.0f, (float) getHeight());

    // --- playhead ------------------------------------------------------------
    if (engine.isPlaying() && engine.getMode() == Transport::Mode::song)
    {
        const auto stepsPerBar = juce::jmax (1, (int) document.getState()[ids::stepsPerBeat]) * 4;
        const auto position = engine.getPlayheadSteps() / (double) stepsPerBar;
        const auto x = (float) headerWidth + (float) position * width;

        g.setColour (Palette::playhead);
        g.drawVerticalLine ((int) x, (float) rulerHeight, (float) getHeight());
    }

    if (trackIndex == 0)
    {
        g.setColour (Palette::textDim);
        g.drawText ("This project has no playlist tracks", getLocalBounds(),
                    juce::Justification::centred);
    }
}

} // namespace dew
