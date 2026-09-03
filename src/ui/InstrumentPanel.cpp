#include "ui/InstrumentPanel.h"

#include "model/PresetLibrary.h"

#include <cmath>

#include "engine/EngineSnapshot.h"
#include "model/Ids.h"
#include "model/ModuleCatalog.h"
#include "model/ProjectEdits.h"
#include "ui/DewLookAndFeel.h"

namespace dew
{

using namespace tokens;

namespace
{

void styleCaption (juce::Label& label, const juce::String& text)
{
    label.setText (text, juce::dontSendNotification);
    label.setFont (tokens::type::font (tokens::type::caption));
    label.setColour (juce::Label::textColourId, tokens::colour::textSecondary);
    label.setJustificationType (juce::Justification::centred);
}

} // namespace

InstrumentPanel::InstrumentPanel (ProjectDocument& d, EditorState& s, SamplePool* pool)
    : document (d), editorState (s), oscSection (d, s), sampleSection (d, pool),
      chainHost (d, s, EffectChainHost::Orientation::vertical)
{
    addChildComponent (sampleSection);

    setComponentID ("instrumentPanel");

    addAndMakeVisible (chainHost);

    titleLabel.setFont (tokens::type::font (tokens::type::title, true));
    titleLabel.setColour (juce::Label::textColourId, tokens::colour::textPrimary);
    addAndMakeVisible (titleLabel);

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
    addAndMakeVisible (mixerBox);
    styleCaption (mixerLabel, "MIXER");
    addAndMakeVisible (mixerLabel);

    const auto ampOf = [this] { return selectedChannel().getChildWithName (ids::INSTRUMENT)
                                                        .getChildWithName (ids::AMP); };
    const auto channelOf = [this] { return selectedChannel(); };

    basePitchSlider.setSliderStyle (juce::Slider::IncDecButtons);
    attachRotary (basePitchSlider, basePitchLabel, "PITCH", channelOf, ids::basePitch, "Change base pitch");

    attachRotary (attackSlider,  attackLabel,  "ATTACK",  ampOf, ids::attack, "Change attack");
    attachRotary (decaySlider,   decayLabel,   "DECAY",   ampOf, ids::decay, "Change decay");
    attachRotary (sustainSlider, sustainLabel, "SUSTAIN", ampOf, ids::sustain, "Change sustain");
    attachRotary (releaseSlider, releaseLabel, "RELEASE", ampOf, ids::release, "Change release");

    attachRotary (volumeSlider, volumeLabel, "VOLUME", channelOf, ids::volume, "Change volume");
    attachRotary (panSlider,    panLabel,    "PAN",    channelOf, ids::pan, "Change pan");

    editorState.addChangeListener (this);
    document.getState().addListener (this);

    refresh();
}

InstrumentPanel::~InstrumentPanel()
{
    editorState.removeChangeListener (this);
    document.getState().removeListener (this);
}

namespace
{

/** The skew that makes the geometric midpoint of a range sit at the middle of a
    control's travel. juce::NormalisableRange takes a skew rather than a curve
    kind, and this is what "logarithmic" means in its terms.
*/
double skewForRange (double minimum, double maximum)
{
    return std::log (0.5) / std::log ((std::sqrt (minimum * maximum) - minimum)
                                      / (maximum - minimum));
}

} // namespace

void InstrumentPanel::setParamMenuHost (const paramMenu::Host* host)
{
    paramMenuHost = host;

    // The chain under the panel gets it too: an effect's knobs are the largest
    // group of spec-built controls in the application, and the panel is the only
    // thing between them and whoever owns the host.
    chainHost.getChain().setParamMenuHost (host);
    oscSection.setParamMenuHost (host);
    sampleSection.setParamMenuHost (host);

    paramMenuTriggers.clear();

    if (host == nullptr || host->document == nullptr)
        return;

    // The rotaries are attached in the constructor, before the host arrives, so
    // this goes back over them rather than only recording the pointer.
    for (const auto& bound : boundRotaries)
        paramMenuTriggers.push_back (
            std::make_unique<paramMenu::Trigger> (
                *bound.slider,
                host->contextFor (bound.owner, requireInstrumentParamSpec (bound.property))));
}

void InstrumentPanel::attachRotary (juce::Slider& slider, juce::Label& label, const juce::String& text,
                                    std::function<juce::ValueTree()> owner,
                                    const juce::Identifier& property,
                                    const juce::String& transactionName)
{
    const auto& spec = requireInstrumentParamSpec (property);

    if (slider.getSliderStyle() != juce::Slider::IncDecButtons)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 58, tokens::size::controlHeightSm);
    }
    else
    {
        slider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 44, tokens::size::controlHeightSm);
    }

    // A logarithmic parameter gets a NormalisableRange, not a plain one. Half a
    // millisecond to ten seconds is four and a half decades; linearly, every
    // usable attack lives in the first one per cent of the travel.
    if (spec.curve == ParamCurve::logarithmic && spec.minimum > 0.0)
        slider.setNormalisableRange ({ spec.minimum, spec.maximum, spec.interval,
                                       skewForRange (spec.minimum, spec.maximum) });
    else
        slider.setRange (spec.minimum, spec.maximum, spec.interval);

    // One transaction per gesture, so dragging a knob is a single undo step
    // rather than several hundred.
    slider.onDragStart = [this] { inDrag = true; gestureActive = false; };
    slider.onDragEnd = [this] { inDrag = false; gestureActive = false; };

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

    boundRotaries.push_back ({ &slider, owner, property });

    styleCaption (label, text);
    addAndMakeVisible (slider);
    addAndMakeVisible (label);
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
    if (tree.hasType (ids::CHANNEL) || tree.hasType (ids::AMP)
        || tree.hasType (ids::MIXER_TRACK))
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
    showingAudio = valid && ProjectEdits::isAudioChannel (channel);

    oscSection.setOwner (showingAudio ? juce::ValueTree()
                                      : channel.getChildWithName (ids::INSTRUMENT));
    sampleSection.setOwner (showingAudio ? channel.getChildWithName (ids::SAMPLE)
                                         : juce::ValueTree());

    oscSection.setVisible (! showingAudio);
    sampleSection.setVisible (showingAudio);

    // Base pitch and the amplitude envelope belong to the oscillators. Leaving
    // them on screen for a recording would offer four controls that do nothing.
    const std::initializer_list<juce::Component*> synthOnly {
        &basePitchSlider, &basePitchLabel,
        &attackSlider, &attackLabel, &decaySlider, &decayLabel,
        &sustainSlider, &sustainLabel, &releaseSlider, &releaseLabel };

    for (auto* c : synthOnly)
        c->setVisible (! showingAudio);

    resized();

    chainHost.setOwner (channel, channel.isValid() ? channel[ids::name].toString()
                                                   : juce::String());

    if (! valid)
    {
        titleLabel.setText ("No channel selected", juce::dontSendNotification);
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

    attackSlider.setValue ((double) amp[ids::attack], juce::dontSendNotification);
    decaySlider.setValue ((double) amp[ids::decay], juce::dontSendNotification);
    sustainSlider.setValue ((double) amp[ids::sustain], juce::dontSendNotification);
    releaseSlider.setValue ((double) amp[ids::release], juce::dontSendNotification);

    volumeSlider.setValue ((double) channel[ids::volume], juce::dontSendNotification);
    panSlider.setValue ((double) channel[ids::pan], juce::dontSendNotification);
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

    for (const auto& preset : presetsForChannel (selectedChannel()))
        items.add (preset.name);

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

    juce::PopupMenu menu;

    for (int i = 0; i < (int) presets.size(); ++i)
        menu.addItem (i + 1, presets[(size_t) i].name);

    menu.setLookAndFeel (&getLookAndFeel());
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetButton),
                        [this] (int choice) { applyPresetChoice (choice); });
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

    const auto row = [&area] (int height) { auto r = area.removeFromTop (height); area.removeFromTop (space::sm); return r; };

    if (showingAudio)
        sampleSection.setBounds (row (SampleSection::requiredHeight));
    else
        oscSection.setBounds (row (oscSection.getRequiredHeight()));

    // Routing and base pitch share a row. The oscillator section costs the panel
    // about 120px more than the single wave combo it replaces, and at the
    // smallest window the app opens at that was the whole effect chain.
    auto routingRow = row (size::knob);

    const auto placeKnob = [] (juce::Rectangle<int> bounds, juce::Label& label, juce::Slider& slider)
    {
        label.setBounds (bounds.removeFromTop (size::knobValue));
        slider.setBounds (bounds);
    };

    if (! showingAudio)
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

    if (! showingAudio)
    {
        auto adsr = row (86);
        const auto knobWidth = adsr.getWidth() / 4;

        placeKnob (adsr.removeFromLeft (knobWidth), attackLabel, attackSlider);
        placeKnob (adsr.removeFromLeft (knobWidth), decayLabel, decaySlider);
        placeKnob (adsr.removeFromLeft (knobWidth), sustainLabel, sustainSlider);
        placeKnob (adsr, releaseLabel, releaseSlider);
    }

    auto levels = row (86);
    placeKnob (levels.removeFromLeft (levels.getWidth() / 2), volumeLabel, volumeSlider);
    placeKnob (levels, panLabel, panSlider);

    chainHost.setBounds (area);
}

} // namespace dew
