#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "i18n/Strings.h"

#include "io/SamplePool.h"
#include "app/ProjectDocument.h"
#include "model/ModuleCatalog.h"
#include "model/Ids.h"
#include "ui/ParamContextMenu.h"
#include "ui/primitives/DewControls.h"

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
class SampleSection : public juce::Component, private juce::ValueTree::Listener
{
public:
    /** Hands this section's knobs what a right-click menu needs. Null means no
        menus. None of these is automatable, so what the menu offers is a reset -
        which is the other half of why a control has one. */
    void setParamMenuHost (const paramMenu::Host*);

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

    static constexpr int requiredHeight = waveformHeight + tokens::space::sm
                                          + 68                      // fade in + fade out
                                          + tokens::space::sm + 68; // transpose + toggles

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

    /** One of the knobs, so a test can drive a whole gesture through it. This
        panel is where the one-undo-step-per-gesture rule was missing. */
    DewKnob& getFadeInKnob() noexcept
    {
        return fadeInKnob;
    }

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;

    void write (const juce::Identifier& property, const juce::var& value,
                const juce::String& transactionName);

    /** Wires a rotary to a property on the SAMPLE node. */
    const paramMenu::Host* paramMenuHost = nullptr;

    void attachKnob (DewKnob&, const juce::Identifier& property,
                     const juce::String& transactionName);

    const SamplePool::Entry* entry() const;

    /** Total frames in the source, or 0. The trim is expressed against this
        rather than against the trimmed region, so dragging one handle does not
        move the other.
    */
    int sourceLength() const;

    int sampleAtX (float x) const;

    enum class Handle
    {
        none,
        start,
        end
    };

    Handle handleAt (juce::Point<float>) const;

    ProjectDocument& document;
    SamplePool* pool;

    juce::ValueTree sample;

    DewKnob fadeInKnob { requireInstrumentParamSpec (ids::fadeInMs) };
    DewKnob fadeOutKnob { requireInstrumentParamSpec (ids::fadeOutMs) };
    DewKnob transposeKnob { requireInstrumentParamSpec (ids::transpose) };

    DewIconButton reverseButton { icons::rewind(), tr (StringId::sample_reverse_help) };
    DewIconButton loopButton { icons::loop(), tr (StringId::sample_loop_help) };

    Handle dragging = Handle::none;

    /** Between a drag's start and its end. */
    bool inDrag = false;

    /** True for every value after the first in one gesture, so the whole drag
        is one undo step rather than one per frame. */
    bool gestureActive = false;
    Handle hovering = Handle::none;

    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleSection)
};

} // namespace dew
