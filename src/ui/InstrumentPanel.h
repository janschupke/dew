#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../model/ProjectDocument.h"
#include "EditorState.h"

namespace dew
{

/** The selected channel's oscillator and envelope.

    Controls write straight into the ValueTree through the UndoManager; the
    engine picks the change up on its next snapshot, so a knob turn is audible
    on the next block without any separate parameter plumbing.
*/
class InstrumentPanel : public juce::Component,
                        private juce::ChangeListener,
                        private juce::ValueTree::Listener
{
public:
    InstrumentPanel (ProjectDocument&, EditorState&);
    ~InstrumentPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void refresh();

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;

    juce::ValueTree selectedChannel() const;

    /** Wires a rotary to a property, opening one undo transaction per gesture. */
    void attachRotary (juce::Slider&, juce::Label&, const juce::String& text,
                       std::function<juce::ValueTree()> owner,
                       const juce::Identifier& property,
                       double minimum, double maximum, double interval,
                       const juce::String& transactionName);

    ProjectDocument& document;
    EditorState& editorState;

    juce::Label titleLabel;

    juce::ComboBox waveBox;
    juce::Label waveLabel;

    juce::Slider octaveSlider { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::Label octaveLabel;

    juce::Slider basePitchSlider { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::Label basePitchLabel;

    juce::Slider attackSlider, decaySlider, sustainSlider, releaseSlider;
    juce::Label attackLabel, decayLabel, sustainLabel, releaseLabel;

    juce::Slider volumeSlider, panSlider;
    juce::Label volumeLabel, panLabel;

    juce::ComboBox mixerBox;
    juce::Label mixerLabel;

    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InstrumentPanel)
};

} // namespace dew
