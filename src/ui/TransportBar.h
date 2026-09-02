#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../engine/AudioEngine.h"
#include "../model/ProjectDocument.h"
#include "EditorState.h"
#include "SignalScope.h"
#include "primitives/DewControls.h"
#include "primitives/DewNumberField.h"

namespace dew
{

/** Play/stop, tempo, pattern-or-song, the current pattern, and the playhead. */
class TransportBar : public juce::Component,
                     private juce::Timer,
                     private juce::ChangeListener,
                     private juce::ValueTree::Listener
{
public:
    TransportBar (ProjectDocument&, AudioEngine&, EditorState&);
    ~TransportBar() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Called when the document is replaced wholesale. */
    void refresh();

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;

    void rebuildPatternList();
    void updatePositionLabel();
    void refreshPatternLength();
    juce::ValueTree currentPattern() const;

    ProjectDocument& document;
    AudioEngine& engine;
    EditorState& editorState;

    DewIconButton playButton { icons::play(), "Play or pause (Space)" };
    DewIconButton stopButton { icons::stop(), "Stop and rewind" };
    DewNumberField tempoField;
    DewButton modeButton { "Pattern", DewButton::Role::normal };

    // Pattern management lives here because the pattern selector does: adding a
    // pattern used to be reachable only from a menu shortcut, which meant it
    // read as "you cannot add more patterns".
    juce::ComboBox patternBox;
    DewIconButton addPatternButton { icons::plus(), "Add a pattern" };
    DewIconButton clonePatternButton { icons::duplicate(), "Duplicate this pattern" };
    DewIconButton deletePatternButton { icons::trash(), "Delete this pattern" };
    DewNumberField patternLengthField;

    juce::Label positionLabel;

    /** The right-hand end of the bar, and the only thing in it that gives way
        when the window narrows.
    */
    SignalScope signalScope;

    bool updatingPatternList = false;
    juce::Array<int> groupDividers;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBar)
};

} // namespace dew
