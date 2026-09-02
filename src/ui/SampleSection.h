#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../engine/SamplePool.h"
#include "../model/ProjectDocument.h"
#include "primitives/DewControls.h"

namespace dew
{

/** An audio channel's sample, and what is done to it on the way out.

    The audio counterpart of OscillatorSection, and deliberately the same shape:
    one setOwner(), one static requiredHeight the host budgets against, one
    write() helper that opens exactly one undo transaction per gesture, and a
    listener filtered by node identity rather than node type because it listens
    to the whole document.

    A synth channel is edited by describing the sound you want; an audio channel
    is edited by pointing at the part of a recording you meant. So the waveform
    is not decoration here - it is the control, and the trim handles are on it
    rather than in a pair of numeric fields beside it.
*/
class SampleSection : public juce::Component,
                      private juce::ValueTree::Listener
{
public:
    /** No EditorState, unlike OscillatorSection: that one keeps a selected
        slot, and a sample has nothing to select between.
    */
    SampleSection (ProjectDocument&, SamplePool*);
    ~SampleSection() override;

    /** Points the section at a channel's SAMPLE node. An invalid tree disables
        it rather than leaving the previous channel's audio on screen.
    */
    void setOwner (juce::ValueTree sampleNode);

    /** Height this section needs, as a constant for the same reason
        OscillatorSection's is: the host has to budget for it before anything
        has been laid out.
    */
    static constexpr int waveformHeight = 60;

    static constexpr int requiredHeight = waveformHeight
                                        + tokens::space::sm + 68    // fade in + fade out
                                        + tokens::space::sm + 68;   // transpose + toggles

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    void refresh();

    // --- for tests -----------------------------------------------------------
    juce::Rectangle<int> getWaveformBounds() const;

    /** Whether there is audio to draw. False shows the empty state instead. */
    bool hasAudio() const;

    /** Where a trim handle sits, in this component's coordinates. */
    float getTrimHandleX (bool start) const;

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;

    void write (const juce::Identifier& property, const juce::var& value,
                const juce::String& transactionName);

    /** Wires a rotary to a property on the SAMPLE node. */
    void attachKnob (DewKnob&, const juce::Identifier& property,
                     const juce::String& transactionName);

    const SamplePool::Entry* entry() const;

    /** Total frames in the source, or 0. The trim is expressed against this
        rather than against the trimmed region, so dragging one handle does not
        move the other.
    */
    int sourceLength() const;

    int sampleAtX (float x) const;

    enum class Handle { none, start, end };

    Handle handleAt (juce::Point<float>) const;

    ProjectDocument& document;
    SamplePool* pool;

    juce::ValueTree sample;

    DewKnob fadeInKnob { "FADE IN", 0.0, 2000.0, 1.0 };
    DewKnob fadeOutKnob { "FADE OUT", 0.0, 2000.0, 1.0 };
    DewKnob transposeKnob { "PITCH", -24.0, 24.0, 1.0 };

    DewIconButton reverseButton { icons::rewind(), "Play the sample backwards" };
    DewIconButton loopButton { icons::loop(), "Loop the sample to fill the clip" };

    Handle dragging = Handle::none;
    Handle hovering = Handle::none;

    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleSection)
};

} // namespace dew
