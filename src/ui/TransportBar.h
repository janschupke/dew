#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../engine/AudioEngine.h"
#include "../model/ProjectDocument.h"
#include "EditorState.h"

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

    void setStatusText (const juce::String&);

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;

    void rebuildPatternList();
    void updatePositionLabel();

    ProjectDocument& document;
    AudioEngine& engine;
    EditorState& editorState;

    juce::TextButton playButton { "Play" };
    juce::TextButton stopButton { "Stop" };
    juce::Slider tempoSlider { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::TextButton modeButton { "Pattern" };
    juce::ComboBox patternBox;
    juce::Label positionLabel;
    juce::Label statusLabel;

    bool updatingPatternList = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBar)
};

} // namespace dew
