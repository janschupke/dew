#include "TransportBar.h"

#include "../model/Ids.h"
#include "DewLookAndFeel.h"

namespace dew
{

TransportBar::TransportBar (ProjectDocument& d, AudioEngine& e, EditorState& s)
    : document (d), engine (e), editorState (s)
{
    setComponentID ("transportBar");
    patternBox.setComponentID ("patternSelector");

    playButton.setClickingTogglesState (false);
    playButton.onClick = [this]
    {
        if (engine.isPlaying())
            engine.stop();
        else
            engine.play();

        playButton.setButtonText (engine.isPlaying() ? "Pause" : "Play");
    };
    addAndMakeVisible (playButton);

    stopButton.onClick = [this]
    {
        engine.stop();
        engine.rewind();
        playButton.setButtonText ("Play");
    };
    addAndMakeVisible (stopButton);

    tempoSlider.setRange (20.0, 300.0, 0.5);
    tempoSlider.setNumDecimalPlacesToDisplay (1);
    tempoSlider.setTextValueSuffix (" bpm");
    tempoSlider.onValueChange = [this]
    {
        auto& undo = document.getUndoManager();
        undo.beginNewTransaction ("Change tempo");
        document.getState().setProperty (ids::tempoBpm, tempoSlider.getValue(), &undo);
    };
    addAndMakeVisible (tempoSlider);

    modeButton.setClickingTogglesState (true);
    modeButton.onClick = [this]
    {
        const auto song = modeButton.getToggleState();
        modeButton.setButtonText (song ? "Song" : "Pattern");
        engine.setMode (song ? Transport::Mode::song : Transport::Mode::pattern);
        engine.rewind();
    };
    addAndMakeVisible (modeButton);

    patternBox.onChange = [this]
    {
        if (updatingPatternList)
            return;

        const auto id = patternBox.getSelectedId();

        if (id > 0)
        {
            editorState.setCurrentPatternId (id);
            engine.setCurrentPatternId (id);
        }
    };
    addAndMakeVisible (patternBox);

    positionLabel.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
    positionLabel.setColour (juce::Label::textColourId, Palette::playhead);
    positionLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (positionLabel);

    statusLabel.setFont (juce::FontOptions (12.0f));
    statusLabel.setColour (juce::Label::textColourId, Palette::textDim);
    statusLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (statusLabel);

    document.getState().addListener (this);
    editorState.addChangeListener (this);

    refresh();
    startTimerHz (30);
}

TransportBar::~TransportBar()
{
    document.getState().removeListener (this);
    editorState.removeChangeListener (this);
}

void TransportBar::refresh()
{
    tempoSlider.setValue ((double) document.getState()[ids::tempoBpm], juce::dontSendNotification);
    rebuildPatternList();

    const auto song = engine.getMode() == Transport::Mode::song;
    modeButton.setToggleState (song, juce::dontSendNotification);
    modeButton.setButtonText (song ? "Song" : "Pattern");
    playButton.setButtonText (engine.isPlaying() ? "Pause" : "Play");
}

void TransportBar::setStatusText (const juce::String& text)
{
    statusLabel.setText (text, juce::dontSendNotification);
}

void TransportBar::rebuildPatternList()
{
    const juce::ScopedValueSetter<bool> quiet (updatingPatternList, true);

    patternBox.clear (juce::dontSendNotification);

    for (const auto& pattern : document.getState())
        if (pattern.hasType (ids::PATTERN))
            patternBox.addItem (pattern[ids::name].toString(), (int) pattern[ids::id]);

    auto id = editorState.getCurrentPatternId();

    if (patternBox.indexOfItemId (id) < 0 && patternBox.getNumItems() > 0)
    {
        id = patternBox.getItemId (0);
        editorState.setCurrentPatternId (id);
        engine.setCurrentPatternId (id);
    }

    patternBox.setSelectedId (id, juce::dontSendNotification);
}

void TransportBar::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (patternBox.getSelectedId() != editorState.getCurrentPatternId())
        patternBox.setSelectedId (editorState.getCurrentPatternId(), juce::dontSendNotification);
}

void TransportBar::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    if (property == ids::tempoBpm)
        tempoSlider.setValue ((double) tree[ids::tempoBpm], juce::dontSendNotification);
    else if (property == ids::name && tree.hasType (ids::PATTERN))
        rebuildPatternList();
}

void TransportBar::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child)
{
    if (child.hasType (ids::PATTERN))
        rebuildPatternList();
}

void TransportBar::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& child, int)
{
    if (child.hasType (ids::PATTERN))
        rebuildPatternList();
}

void TransportBar::timerCallback()
{
    updatePositionLabel();
}

void TransportBar::updatePositionLabel()
{
    const auto steps = engine.getPlayheadSteps();
    const auto stepsPerBeat = juce::jmax (1, (int) document.getState()[ids::stepsPerBeat]);
    const auto stepsPerBar = stepsPerBeat * 4;

    const auto totalSteps = (int) steps;
    const auto bar = totalSteps / stepsPerBar + 1;
    const auto beat = (totalSteps % stepsPerBar) / stepsPerBeat + 1;
    const auto tick = totalSteps % stepsPerBeat + 1;

    positionLabel.setText (juce::String::formatted ("%03d:%d:%d", bar, beat, tick),
                           juce::dontSendNotification);
}

void TransportBar::paint (juce::Graphics& g)
{
    g.fillAll (Palette::panel);
    g.setColour (Palette::line);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
}

void TransportBar::resized()
{
    auto area = getLocalBounds().reduced (8, 6);

    playButton.setBounds (area.removeFromLeft (72));
    area.removeFromLeft (6);
    stopButton.setBounds (area.removeFromLeft (64));
    area.removeFromLeft (14);

    tempoSlider.setBounds (area.removeFromLeft (150));
    area.removeFromLeft (14);

    modeButton.setBounds (area.removeFromLeft (84));
    area.removeFromLeft (6);
    patternBox.setBounds (area.removeFromLeft (150));
    area.removeFromLeft (14);

    positionLabel.setBounds (area.removeFromLeft (90));

    statusLabel.setBounds (area);
}

} // namespace dew
