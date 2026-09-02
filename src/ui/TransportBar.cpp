#include "TransportBar.h"

#include "../model/Ids.h"
#include "../model/ProjectEdits.h"
#include "DewLookAndFeel.h"
#include "design/Tokens.h"

namespace dew
{

TransportBar::TransportBar (ProjectDocument& d, AudioEngine& e, EditorState& s)
    : document (d), engine (e), editorState (s)
{
    setComponentID ("transportBar");
    patternBox.setComponentID ("patternSelector");

    playButton.onClick = [this]
    {
        if (engine.isPlaying())
            engine.stop();
        else
            engine.play();

        playButton.setIcon (engine.isPlaying() ? icons::pause() : icons::play());
    };
    addAndMakeVisible (playButton);

    stopButton.onClick = [this]
    {
        engine.stop();
        engine.rewind();
        playButton.setIcon (icons::play());
    };
    addAndMakeVisible (stopButton);

    tempoField.setRange (20.0, 300.0, 0.1);
    tempoField.setNumDecimalPlaces (1);
    tempoField.setSuffix (" bpm");
    tempoField.setTooltip ("Tempo - drag up and down, or double-click to type");
    tempoField.onEditStart = [this]
    {
        document.getUndoManager().beginNewTransaction ("Change tempo");
    };
    tempoField.onValueChange = [this]
    {
        auto& undo = document.getUndoManager();
        document.getState().setProperty (ids::tempoBpm, tempoField.getValue(), &undo);
    };
    addAndMakeVisible (tempoField);

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

    addPatternButton.onClick = [this]
    {
        auto& undo = document.getUndoManager();
        undo.beginNewTransaction ("Add pattern");
        const auto pattern = ProjectEdits::addPattern (document.getState(), &undo);
        editorState.setCurrentPatternId ((int) pattern[ids::id]);
        engine.setCurrentPatternId ((int) pattern[ids::id]);
    };
    addAndMakeVisible (addPatternButton);

    clonePatternButton.onClick = [this]
    {
        auto& undo = document.getUndoManager();
        undo.beginNewTransaction ("Duplicate pattern");
        const auto copy = ProjectEdits::duplicatePattern (document.getState(),
                                                          currentPattern(), &undo);

        if (copy.isValid())
        {
            editorState.setCurrentPatternId ((int) copy[ids::id]);
            engine.setCurrentPatternId ((int) copy[ids::id]);
        }
    };
    addAndMakeVisible (clonePatternButton);

    deletePatternButton.onClick = [this]
    {
        auto& undo = document.getUndoManager();
        undo.beginNewTransaction ("Delete pattern");
        ProjectEdits::removePattern (document.getState(), currentPattern(), &undo);
    };
    addAndMakeVisible (deletePatternButton);

    patternLengthField.setRange (1.0, 256.0, 1.0);
    patternLengthField.setNumDecimalPlaces (0);
    // A suffix rather than a caption, matching the tempo field beside it: a
    // caption reserves 12px above the value, which made this the only control
    // in the bar that could not share the common height.
    patternLengthField.setSuffix (" steps");
    patternLengthField.setTooltip ("Pattern length - drag up and down, or double-click to type");
    patternLengthField.onEditStart = [this]
    {
        document.getUndoManager().beginNewTransaction ("Change pattern length");
    };
    patternLengthField.onValueChange = [this]
    {
        auto pattern = currentPattern();

        if (pattern.isValid())
            pattern.setProperty (ids::lengthSteps, (int) patternLengthField.getValue(),
                                 &document.getUndoManager());
    };
    addAndMakeVisible (patternLengthField);

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
    tempoField.setValue ((double) document.getState()[ids::tempoBpm], juce::dontSendNotification);
    rebuildPatternList();
    refreshPatternLength();

    const auto song = engine.getMode() == Transport::Mode::song;
    modeButton.setToggleState (song, juce::dontSendNotification);
    modeButton.setButtonText (song ? "Song" : "Pattern");
    playButton.setIcon (engine.isPlaying() ? icons::pause() : icons::play());

    // Otherwise the readout is blank until the first timer tick.
    updatePositionLabel();
}

juce::ValueTree TransportBar::currentPattern() const
{
    return ProjectEdits::findPattern (document.getState(), editorState.getCurrentPatternId());
}

void TransportBar::refreshPatternLength()
{
    const auto pattern = currentPattern();

    patternLengthField.setEnabled (pattern.isValid());

    if (pattern.isValid())
        patternLengthField.setValue ((double) (int) pattern[ids::lengthSteps],
                                     juce::dontSendNotification);

    // Deleting the only pattern would leave nothing to edit or play.
    deletePatternButton.setEnabled (patternBox.getNumItems() > 1);
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

    refreshPatternLength();
}

void TransportBar::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    if (property == ids::tempoBpm)
        tempoField.setValue ((double) tree[ids::tempoBpm], juce::dontSendNotification);
    else if (property == ids::name && tree.hasType (ids::PATTERN))
        rebuildPatternList();
    else if (property == ids::lengthSteps && tree.hasType (ids::PATTERN))
        refreshPatternLength();
}

void TransportBar::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child)
{
    if (child.hasType (ids::PATTERN))
    {
        rebuildPatternList();
        refreshPatternLength();
    }
}

void TransportBar::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& child, int)
{
    if (child.hasType (ids::PATTERN))
    {
        rebuildPatternList();
        refreshPatternLength();
    }
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
    g.fillAll (tokens::colour::surface);

    g.setColour (tokens::colour::dividerStrong);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    // Separators between the transport, the pattern controls and the readout.
    for (auto x : groupDividers)
    {
        g.setColour (tokens::colour::divider);
        g.drawVerticalLine (x, 8.0f, (float) getHeight() - 8.0f);
    }
}

void TransportBar::resized()
{
    using namespace tokens;

    groupDividers.clear();

    auto area = getLocalBounds().reduced (space::md, space::sm);
    const auto controlHeight = juce::jmin (size::controlHeight, area.getHeight());

    const auto place = [&area, controlHeight] (juce::Component& c, int width)
    {
        c.setBounds (area.removeFromLeft (width).withHeight (controlHeight));
        area.removeFromLeft (space::xs);
    };

    place (playButton, 30);
    place (stopButton, 30);
    area.removeFromLeft (space::sm);
    place (tempoField, 96);
    area.removeFromLeft (space::sm);
    place (modeButton, 78);

    area.removeFromLeft (space::sm);
    groupDividers.add (area.getX());
    area.removeFromLeft (space::md);

    place (patternBox, 148);
    place (addPatternButton, 26);
    place (clonePatternButton, 26);
    place (deletePatternButton, 26);
    place (patternLengthField, 84);
    area.removeFromLeft (space::sm);

    groupDividers.add (area.getX());
    area.removeFromLeft (space::md);

    positionLabel.setBounds (area.removeFromLeft (84).withHeight (controlHeight));
    statusLabel.setBounds (area.withHeight (controlHeight));
}

} // namespace dew
