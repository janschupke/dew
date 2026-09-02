#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/ProjectDocument.h"
#include "ui/EditorState.h"
#include "ui/EffectChainHost.h"
#include "ui/OscillatorSection.h"
#include "ui/SampleSection.h"

namespace dew
{

/** The selected channel's sound, whichever kind of channel it is.

    Controls write straight into the ValueTree through the UndoManager; the
    engine picks the change up on its next snapshot, so a knob turn is audible
    on the next block without any separate parameter plumbing.

    Two faces, one panel. A synth channel shows its oscillators and envelope; an
    audio channel shows its waveform, trim, fades and pitch. Everything below
    that - mixer routing, volume, pan and the effect chain - is shared, because
    a channel is the same thing downstream of where its samples come from. Two
    separate panels would have duplicated all of it and then drifted.
*/
class InstrumentPanel : public juce::Component,
                        private juce::ChangeListener,
                        private juce::ValueTree::Listener
{
public:
    /** @param pool  audio for the waveform display. Null is allowed - the
                      section draws its empty state - which is what lets a test
                      or dew_shot build the panel without a sample pool.
    */
    InstrumentPanel (ProjectDocument&, EditorState&, SamplePool* pool = nullptr);
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

    /** True between a knob's onDragStart and onDragEnd - see attachRotary. */
    bool dragging = false;

    juce::Label titleLabel;

    /** The channel's oscillator slots. Its own component: it carries its own
        selection, its own listener scoped to one instrument's nodes and its own
        test seams, none of which the rest of this panel has any use for.
    */
    OscillatorSection oscSection;

    /** The audio channel's face of the panel. Exactly one of this and
        oscSection is visible; refresh() is the single place that decides.
    */
    SampleSection sampleSection;

    juce::Slider basePitchSlider { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::Label basePitchLabel;

    juce::Slider attackSlider, decaySlider, sustainSlider, releaseSlider;
    juce::Label attackLabel, decayLabel, sustainLabel, releaseLabel;

    juce::Slider volumeSlider, panSlider;
    juce::Label volumeLabel, panLabel;

    juce::ComboBox mixerBox;
    juce::Label mixerLabel;

    /** The selected channel's effect chain, edited by the same component the
        mixer uses - a channel and a mixer track carry the same EFFECT children.

        A column here, where the panel is narrow and a card that folds away is
        how four effects fit. The mixer points the same component the other way
        round; the host owns the scrolling either way, because a full chain with
        cards open is taller than the panel and used to be given "whatever is
        left" and clipped in silence.
    */
    EffectChainHost chainHost;

    bool updating = false;

    /** Whether the panel is showing its audio face. Cached from the document so
        that resized() and refresh() cannot disagree about the row stack.
    */
    bool showingAudio = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InstrumentPanel)
};

} // namespace dew
