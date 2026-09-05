#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/ProjectDocument.h"
#include "ui/EditorState.h"
#include <memory>
#include <vector>
#include "ui/EffectChainHost.h"
#include "ui/ParamContextMenu.h"
#include "ui/OscillatorSection.h"
#include "ui/SampleSection.h"
#include "ui/SoundFontSection.h"

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
    /** Hands this panel and the chain under it what a right-click menu needs.
        Null means no menus. */
    void setParamMenuHost (const paramMenu::Host*);

    /** @param pool  audio for the waveform display. Null is allowed - the
                      section draws its empty state - which is what lets a test
                      or dew_shot build the panel without a sample pool.
    */
    InstrumentPanel (ProjectDocument&, EditorState&, SamplePool* pool = nullptr,
                     SoundFontPool* soundFonts = nullptr);
    ~InstrumentPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** How tall the panel needs to be to show everything it is holding.

        Mirrors resized(), which is the only way the two can be trusted to
        agree - the same reason RenderPanel::getRequiredHeight says so.

        It exists because the panel used to give the effect chain "whatever is
        left", and at the smallest window the app can open there is nothing
        left: the chain vanished and the last knob row was cut in half. Nothing
        said so, because a rectangle removed from an exhausted one is simply
        empty. MainComponent scrolls the panel when it does not fit rather than
        letting it clip, and this is the height it scrolls.
    */
    int getRequiredHeight() const;

    void refresh();

    /** What the preset button would offer, and what choosing item `choice`
        does. A menu cannot be driven headlessly - see MenuSeam.h. */
    juce::StringArray presetMenuItems() const;
    bool applyPresetChoice (int choice);

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;

    juce::ValueTree selectedChannel() const;

    void showPresetMenu();

    /** Wires a rotary to a property, opening one undo transaction per gesture.

        The range, the step and the curve come from the catalog rather than
        from this call: the panel used to state them here, the engine stated
        them again as a clamp, and they had drifted - an attack knob that
        stopped at two seconds on an engine that renders ten.
    */
    const paramMenu::Host* paramMenuHost = nullptr;

    /** Which node and property each rotary was attached to.

        Recorded as they are built so setParamMenuHost can go back over them: the
        panel is constructed before the host exists, and a knob whose menu was
        only wired at construction would be a knob with no menu at all.
    */
    struct BoundRotary
    {
        juce::Slider* slider = nullptr;

        /** Set when the slider is a DewKnob's own, which is every one of these
            but the base-pitch stepper. A knob carries its own onContextMenu
            hook, so it wants paramMenu::attachTo; a bare slider has no hook and
            needs a Trigger listening to it instead. */
        DewKnob* knob = nullptr;

        std::function<juce::ValueTree()> owner;
        juce::Identifier property;
    };

    std::vector<BoundRotary> boundRotaries;

    /** Owned here, and destroyed before the sliders they watch. */
    std::vector<std::unique_ptr<paramMenu::Trigger>> paramMenuTriggers;

    /** The base pitch stepper, which is an IncDecButtons slider and not a knob:
        a semitone is a number you nudge, not a sweep. */
    void attachStepper (juce::Slider&, DewLabel&, const juce::String& text,
                        std::function<juce::ValueTree()> owner, const juce::Identifier& property,
                        const juce::String& transactionName);

    /** The same binding for a DewKnob, which carries its own caption and its own
        readout, so there is no label to pass and no text box to configure. */
    void attachKnob (DewKnob&, std::function<juce::ValueTree()> owner,
                     const juce::Identifier& property, const juce::String& transactionName);

    /** Shared by both: the write, the undo transaction and the param menu. */
    void bindRotary (juce::Slider&, DewKnob*, std::function<juce::ValueTree()> owner,
                     const juce::Identifier& property, const juce::String& transactionName);

    ProjectDocument& document;
    EditorState& editorState;

    /** True between a knob's onDragStart and onDragEnd - see attachRotary. */
    bool inDrag = false;
    bool gestureActive = false;

    juce::Label titleLabel;

    /** Loads a factory preset onto the selected channel.

        A button opening a menu rather than a ComboBox, and deliberately: a
        combo shows a CURRENT selection, and with no user save there is no
        honest "modified" state to show once a knob has been touched. A button
        promises only what it does - load one.
    */
    DewButton presetButton { "Preset" };

    /** The channel's oscillator slots. Its own component: it carries its own
        selection, its own listener scoped to one instrument's nodes and its own
        test seams, none of which the rest of this panel has any use for.
    */
    OscillatorSection oscSection;

    /** The audio channel's face of the panel. Exactly one of this and
        oscSection is visible; refresh() is the single place that decides.
    */
    SampleSection sampleSection;

    /** The soundfont channel's face. Exactly one of the three is visible;
        refresh() is the single place that decides. */
    SoundFontSection soundFontSection;

    DewSlider basePitchSlider { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    DewLabel basePitchLabel;

    /** The envelope and the levels, as the same control the oscillator section
        and every effect card already use.

        These were six bare juce::Sliders with six juce::Labels and a JUCE text
        box, in an 86px row - so the panel drew two sizes of knob, one of them
        in a different painter, one above the other. Built from the catalog's
        ParamSpec like every other DewKnob, which is what stops a range being
        stated here a second time and drifting from the engine's own clamp.
    */
    DewKnob attackKnob { requireInstrumentParamSpec (ids::attack) };
    DewKnob decayKnob { requireInstrumentParamSpec (ids::decay) };
    DewKnob sustainKnob { requireInstrumentParamSpec (ids::sustain) };
    DewKnob releaseKnob { requireInstrumentParamSpec (ids::release) };

    DewKnob volumeKnob { requireInstrumentParamSpec (ids::volume) };
    DewKnob panKnob { requireInstrumentParamSpec (ids::pan) };

    DewDropdown mixerBox;
    DewLabel mixerLabel;

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

    /** Which face the panel is showing, cached from the document for the reason
        the bool it replaced was cached: resized() and refresh() must not be
        able to disagree about the row stack. A bool could answer "synth or
        audio"; a third kind of instrument needs the type itself. */
    InstrumentType showing = InstrumentType::synth;

    /** Whether there is a channel to show at all. Separate from the type,
        because an invalid selection is not a kind of instrument. */
    bool showingAny = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InstrumentPanel)
};

} // namespace dew
