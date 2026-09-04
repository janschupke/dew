#include "ui/InstrumentPanel.h"

#include "i18n/Strings.h"
#include "model/PresetLibrary.h"

#include <cmath>

#include "engine/EngineSnapshot.h"
#include "model/Ids.h"
#include "model/ModuleCatalog.h"
#include "model/ProjectEdits.h"
#include "ui/design/Cursors.h"
#include "ui/design/DewLookAndFeel.h"

namespace dew
{

using namespace tokens;

namespace
{

} // namespace

InstrumentPanel::InstrumentPanel (ProjectDocument& d, EditorState& s, SamplePool* pool,
                                  SoundFontPool* soundFonts)
    : document (d)
    , editorState (s)
    , oscSection (d, s)
    , sampleSection (d, pool)
    , soundFontSection (d, soundFonts)
    , chainHost (d, s, EffectChainHost::Orientation::vertical)
{
    addChildComponent (sampleSection);
    addChildComponent (soundFontSection);

    setComponentID ("instrumentPanel");
    // A name and a PLACE in the tree a screen reader is given. focusContainer,
    // not keyboardFocusContainer: the two flags are independent, and the second
    // would confine the tab key to this panel with no key to leave it - a
    // keyboard trap, which is worse than the flat tab order it would tidy.
    setTitle (tr (StringId::instrument_title));
    setFocusContainerType (FocusContainerType::focusContainer);

    addAndMakeVisible (chainHost);

    titleLabel.setFont (tokens::type::font (tokens::type::title, true));
    addAndMakeVisible (titleLabel);

    presetButton.setTooltip (tr (StringId::instrument_preset_help));
    presetButton.onClick = [this] { showPresetMenu(); };
    addAndMakeVisible (presetButton);

    // The oscillator section's height depends on the mode of the slot it is
    // showing, and that mode lives on an OSC node this panel does not listen to
    // - by design, since a change to another channel's oscillator is none of
    // its business. So the section says when its height moved.
    oscSection.onHeightChanged = [this] { resized(); };
    addAndMakeVisible (oscSection);

    mixerBox.onChange = [this]
    {
        if (updating)
            return;

        auto channel = selectedChannel();

        if (! channel.isValid() || mixerBox.getSelectedId() <= 0)
            return;

        ProjectEdits::setProperty (channel, ids::mixerTrackId, mixerBox.getSelectedId(),
                                   &document.getUndoManager(), "Route channel");
    };
    mixerBox.setTooltip (tr (StringId::instrument_mixer_help));
    addAndMakeVisible (mixerBox);
    styleCaption (mixerLabel, "MIXER");
    addAndMakeVisible (mixerLabel);

    const auto ampOf = [this]
    { return selectedChannel().getChildWithName (ids::INSTRUMENT).getChildWithName (ids::AMP); };
    const auto channelOf = [this] { return selectedChannel(); };

    // Clickable rather than value: a stepper's visible parts are two little
    // buttons, and it is pressed rather than dragged.
    basePitchSlider.setMouseCursor (cursor::clickable);
    basePitchSlider.setSliderStyle (juce::Slider::IncDecButtons);
    attachStepper (basePitchSlider, basePitchLabel, "PITCH", channelOf, ids::basePitch,
                   "Change base pitch");

    attachKnob (attackKnob, ampOf, ids::attack, "Change attack");
    attachKnob (decayKnob, ampOf, ids::decay, "Change decay");
    attachKnob (sustainKnob, ampOf, ids::sustain, "Change sustain");
    attachKnob (releaseKnob, ampOf, ids::release, "Change release");

    attachKnob (volumeKnob, channelOf, ids::volume, "Change volume");
    attachKnob (panKnob, channelOf, ids::pan, "Change pan");

    editorState.addChangeListener (this);
    document.getState().addListener (this);

    refresh();
}

InstrumentPanel::~InstrumentPanel()
{
    editorState.removeChangeListener (this);
    document.getState().removeListener (this);
}

// The log-skew helper that used to live here has gone with the sliders it
// served: DewKnob works it out from the ParamSpec's curve, once, for every knob
// in the application rather than for these six.

void InstrumentPanel::setParamMenuHost (const paramMenu::Host* host)
{
    paramMenuHost = host;

    // The chain under the panel gets it too: an effect's knobs are the largest
    // group of spec-built controls in the application, and the panel is the only
    // thing between them and whoever owns the host.
    chainHost.getChain().setParamMenuHost (host);
    oscSection.setParamMenuHost (host);
    sampleSection.setParamMenuHost (host);
    soundFontSection.setParamMenuHost (host);

    paramMenuTriggers.clear();

    if (host == nullptr || host->document == nullptr)
        return;

    // The rotaries are attached in the constructor, before the host arrives, so
    // this goes back over them rather than only recording the pointer.
    for (const auto& bound : boundRotaries)
    {
        const auto& spec = requireInstrumentParamSpec (bound.property);

        // A DewKnob has its own hook, and it has to be the thing that gets it:
        // the coverage gate asks every knob in the window whether it carries
        // one, and a Trigger listening to the slider inside would answer no.
        if (bound.knob != nullptr)
            paramMenu::attachTo (host, *bound.knob, bound.owner, spec);
        else
            paramMenuTriggers.push_back (std::make_unique<paramMenu::Trigger> (
                *bound.slider, host->contextFor (bound.owner, spec)));
    }
}

void InstrumentPanel::bindRotary (juce::Slider& slider, DewKnob* knob,
                                  std::function<juce::ValueTree()> owner,
                                  const juce::Identifier& property,
                                  const juce::String& transactionName)
{
    const auto& spec = requireInstrumentParamSpec (property);

    // One transaction per gesture, so dragging a knob is a single undo step
    // rather than several hundred.
    slider.onDragStart = [this]
    {
        inDrag = true;
        gestureActive = false;
    };
    slider.onDragEnd = [this]
    {
        inDrag = false;
        gestureActive = false;
    };

    slider.onValueChange = [this, &slider, &spec, owner, property, transactionName]
    {
        if (updating)
            return;

        auto tree = owner();

        if (! tree.isValid())
            return;

        // Integer-valued properties must stay integers in the file: writing a
        // double would change the JSON from `0` to `0.0` and, worse, make the
        // schema's type coercion do the rounding instead of this code.
        const juce::var value = spec.integral ? juce::var ((int) slider.getValue())
                                              : juce::var (slider.getValue());

        ProjectEdits::setProperty (tree, property, value, &document.getUndoManager(),
                                   transactionName, gestureActive);

        gestureActive = inDrag;
    };

    boundRotaries.push_back ({ &slider, knob, owner, property });
}

void InstrumentPanel::attachStepper (juce::Slider& slider, DewLabel& label,
                                     const juce::String& text,
                                     std::function<juce::ValueTree()> owner,
                                     const juce::Identifier& property,
                                     const juce::String& transactionName)
{
    const auto& spec = requireInstrumentParamSpec (property);

    slider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 44, tokens::size::controlHeightSm);
    slider.setRange (spec.minimum, spec.maximum, spec.interval);

    bindRotary (slider, nullptr, std::move (owner), property, transactionName);

    styleCaption (label, text);
    addAndMakeVisible (slider);
    addAndMakeVisible (label);
}

void InstrumentPanel::attachKnob (DewKnob& knob, std::function<juce::ValueTree()> owner,
                                  const juce::Identifier& property,
                                  const juce::String& transactionName)
{
    // The caption, the range, the step, the decimals, whether it is bipolar and
    // whether it sweeps logarithmically all came from the ParamSpec when the
    // knob was constructed. Only the WRITE is left, and it is the same write a
    // stepper does - hence one bindRotary under both.
    bindRotary (knob.getSlider(), &knob, std::move (owner), property, transactionName);
    addAndMakeVisible (knob);
}

juce::ValueTree InstrumentPanel::selectedChannel() const
{
    return ProjectEdits::findChannel (document.getState(), editorState.getSelectedChannelId());
}

void InstrumentPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void InstrumentPanel::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&)
{
    // Not ids::OSC: the oscillator section listens for its own nodes, by
    // identity rather than by type - this filter would fire for every channel's.
    if (tree.hasType (ids::CHANNEL) || tree.hasType (ids::AMP) || tree.hasType (ids::MIXER_TRACK))
        refresh();
}

void InstrumentPanel::refresh()
{
    const juce::ScopedValueSetter<bool> quiet (updating, true);

    const auto channel = selectedChannel();
    const auto valid = channel.isValid();

    setEnabled (valid);

    // The one place the panel decides which face it is showing. resized() reads
    // the cached answer rather than asking the document again, so the layout
    // and the visibility can never disagree.
    showingAny = valid;
    showing = valid ? ProjectEdits::instrumentTypeOf (channel).value_or (InstrumentType::synth)
                    : InstrumentType::synth;

    const auto faceFor = [this, &channel] (InstrumentType type, const juce::Identifier& node)
    { return showingAny && showing == type ? channel.getChildWithName (node) : juce::ValueTree(); };

    oscSection.setOwner (faceFor (InstrumentType::synth, ids::INSTRUMENT));
    sampleSection.setOwner (faceFor (InstrumentType::audio, ids::SAMPLE));
    soundFontSection.setOwner (faceFor (InstrumentType::soundfont, ids::SOUNDFONT));

    oscSection.setVisible (showing == InstrumentType::synth);
    sampleSection.setVisible (showing == InstrumentType::audio);
    soundFontSection.setVisible (showing == InstrumentType::soundfont);

    // Base pitch is what a step written on the grid is pitched at, so every
    // channel that takes notes has one. The amplitude envelope belongs to the
    // oscillators alone: a soundfont region carries its own, and a second one
    // stacked on top would be two places holding the same fact.
    const std::initializer_list<juce::Component*> envelopeOnly { &attackKnob, &decayKnob,
                                                                 &sustainKnob, &releaseKnob };

    for (auto* c : envelopeOnly)
        c->setVisible (showing == InstrumentType::synth);

    for (auto* c : { (juce::Component*) &basePitchSlider, (juce::Component*) &basePitchLabel })
        c->setVisible (ProjectEdits::playsNotes (channel));

    resized();

    chainHost.setOwner (channel,
                        channel.isValid() ? channel[ids::name].toString() : juce::String());

    if (! valid)
    {
        titleLabel.setText (tr (StringId::instrument_empty), juce::dontSendNotification);
        return;
    }

    titleLabel.setText (channel[ids::name].toString(), juce::dontSendNotification);

    const auto amp = channel.getChildWithName (ids::INSTRUMENT).getChildWithName (ids::AMP);

    mixerBox.clear (juce::dontSendNotification);

    for (const auto& track : document.getState().getChildWithName (ids::MIXER))
        if (track.hasType (ids::MIXER_TRACK))
            mixerBox.addItem (track[ids::name].toString(), (int) track[ids::id]);

    mixerBox.setSelectedId ((int) channel[ids::mixerTrackId], juce::dontSendNotification);

    basePitchSlider.setValue ((double) channel[ids::basePitch], juce::dontSendNotification);

    attackKnob.setValue ((double) amp[ids::attack], juce::dontSendNotification);
    decayKnob.setValue ((double) amp[ids::decay], juce::dontSendNotification);
    sustainKnob.setValue ((double) amp[ids::sustain], juce::dontSendNotification);
    releaseKnob.setValue ((double) amp[ids::release], juce::dontSendNotification);

    volumeKnob.setValue ((double) channel[ids::volume], juce::dontSendNotification);
    panKnob.setValue ((double) channel[ids::pan], juce::dontSendNotification);
}

void InstrumentPanel::paint (juce::Graphics& g)
{
    g.fillAll (tokens::colour::surface);
    g.setColour (tokens::colour::divider);
    g.drawVerticalLine (0, 0.0f, (float) getHeight());
}

namespace
{

/** The presets for a channel, which kind it is decides. An unknown `source`
    offers nothing rather than guessing - buildSnapshot has already warned. */
std::vector<Preset> presetsForChannel (const juce::ValueTree& channel)
{
    if (const auto type = instrumentTypeFor (channel[ids::source].toString()))
        return PresetLibrary::presetsFor (*type);

    return {};
}

} // namespace

juce::StringArray InstrumentPanel::presetMenuItems() const
{
    juce::StringArray items;

    // Name and description, as one row. The description was authored,
    // serialised into every .dewpreset and shown to nobody for the whole life
    // of the feature; a picker that says only "Pluck" makes you audition the
    // list to find out what is in it.
    for (const auto& preset : presetsForChannel (selectedChannel()))
        items.add (DewLookAndFeel::menuRow (PresetLibrary::displayName (preset),
                                            PresetLibrary::describe (preset)));

    return items;
}

bool InstrumentPanel::applyPresetChoice (int choice)
{
    const auto channel = selectedChannel();
    const auto presets = presetsForChannel (channel);

    if (choice < 1 || choice > (int) presets.size())
        return false;

    return ProjectEdits::applyInstrumentPreset (channel, presets[(size_t) (choice - 1)],
                                                &document.getUndoManager());
}

void InstrumentPanel::showPresetMenu()
{
    const auto presets = presetsForChannel (selectedChannel());

    if (presets.empty())
        return;

    // Built from the same list the headless seam returns, not from a second
    // walk over the presets. The two had already drifted once: presetMenuItems
    // was the thing the tests read and showPresetMenu was the thing a person
    // saw, and only one of them had been taught to translate a name.
    juce::PopupMenu menu;
    const auto items = presetMenuItems();

    for (int i = 0; i < items.size(); ++i)
        menu.addItem (i + 1, items[i]);

    menu.setLookAndFeel (&getLookAndFeel());
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetButton),
                        [this] (int choice) { applyPresetChoice (choice); });
}

int InstrumentPanel::getRequiredHeight() const
{
    // Mirrors resized() row for row. Every rung it names is the one resized()
    // removes, in the same order, so the two cannot drift without the panel
    // visibly disagreeing with its own scrollbar.
    const auto faceHeight = [this]
    {
        switch (showing)
        {
            case InstrumentType::synth: return oscSection.getRequiredHeight();
            case InstrumentType::audio: return SampleSection::requiredHeight;
            case InstrumentType::soundfont: return SoundFontSection::requiredHeight;
        }

        return 0;
    }();

    // The face, the routing row, the level knobs, and the envelope knobs when
    // there is an envelope - each followed by the gap `row` leaves behind.
    auto rows = faceHeight + size::knob + size::knobRow + 3 * space::sm;

    if (showing == InstrumentType::synth)
        rows += size::knobRow + space::sm;

    return space::md * 2 + size::iconButton + space::sm + rows + chainHost.getPreferredHeight();
}

void InstrumentPanel::resized()
{
    auto area = getLocalBounds().reduced (space::md);

    auto titleRow = area.removeFromTop (size::iconButton);

    // The button on the right of the title, at the width the design system
    // gives a labelled control - the title takes whatever is left, which is
    // what it did before there was anything beside it.
    presetButton.setBounds (titleRow.removeFromRight (size::gutterLabel));
    titleRow.removeFromRight (space::sm);
    titleLabel.setBounds (titleRow);

    area.removeFromTop (space::sm);

    const auto row = [&area] (int height)
    {
        auto r = area.removeFromTop (height);
        area.removeFromTop (space::sm);
        return r;
    };

    switch (showing)
    {
        case InstrumentType::synth:
            oscSection.setBounds (row (oscSection.getRequiredHeight()));
            break;

        case InstrumentType::audio:
            sampleSection.setBounds (row (SampleSection::requiredHeight));
            break;

        case InstrumentType::soundfont:
            soundFontSection.setBounds (row (SoundFontSection::requiredHeight));
            break;
    }

    // Routing and base pitch share a row. The oscillator section costs the panel
    // about 120px more than the single wave combo it replaces, and at the
    // smallest window the app opens at that was the whole effect chain.
    auto routingRow = row (size::knob);

    if (showing != InstrumentType::audio)
    {
        // The pitch stepper is measured from the right and the combo takes what
        // is left: an inc/dec pair given "half of whatever remains" is the one
        // control here that clips rather than shrinking.
        basePitchSlider.setBounds (routingRow.removeFromRight (96).reduced (0, space::md));
        basePitchLabel.setBounds (routingRow.removeFromRight (38));
        routingRow.removeFromRight (space::md);
    }

    mixerLabel.setBounds (routingRow.removeFromLeft (46));
    mixerBox.setBounds (routingRow.reduced (0, space::md));

    // knobRow, like every other row of knobs in the application. It was 86,
    // which is the height the JUCE text box under each one needed - a fifth
    // dimension, in the panel that sits beside the four that use the rung.
    const auto placeKnobs = [] (juce::Rectangle<int> bounds, std::initializer_list<DewKnob*> knobs)
    {
        const auto cell = bounds.getWidth() / (int) knobs.size();

        for (auto* knob : knobs)
            knob->setBounds (bounds.removeFromLeft (cell).reduced (space::xxs, 0));
    };

    if (showing == InstrumentType::synth)
        placeKnobs (row (size::knobRow), { &attackKnob, &decayKnob, &sustainKnob, &releaseKnob });

    placeKnobs (row (size::knobRow), { &volumeKnob, &panKnob });

    chainHost.setBounds (area);
}

} // namespace dew
