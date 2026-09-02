#include "InstrumentPanel.h"

#include "../engine/EngineSnapshot.h"
#include "../model/Ids.h"
#include "../model/ProjectEdits.h"
#include "DewLookAndFeel.h"

namespace dew
{

namespace
{

void styleCaption (juce::Label& label, const juce::String& text)
{
    label.setText (text, juce::dontSendNotification);
    label.setFont (tokens::type::font (tokens::type::caption));
    label.setColour (juce::Label::textColourId, Palette::textDim);
    label.setJustificationType (juce::Justification::centred);
}

} // namespace

InstrumentPanel::InstrumentPanel (ProjectDocument& d, EditorState& s)
    : document (d), editorState (s), oscSection (d, s),
      chainHost (d, s, EffectChainHost::Orientation::vertical)
{
    setComponentID ("instrumentPanel");

    addAndMakeVisible (chainHost);

    titleLabel.setFont (tokens::type::font (tokens::type::title, true));
    titleLabel.setColour (juce::Label::textColourId, Palette::text);
    addAndMakeVisible (titleLabel);

    addAndMakeVisible (oscSection);

    mixerBox.onChange = [this]
    {
        if (updating)
            return;

        auto channel = selectedChannel();

        if (! channel.isValid() || mixerBox.getSelectedId() <= 0)
            return;

        auto& undo = document.getUndoManager();
        undo.beginNewTransaction ("Route channel");
        channel.setProperty (ids::mixerTrackId, mixerBox.getSelectedId(), &undo);
    };
    addAndMakeVisible (mixerBox);
    styleCaption (mixerLabel, "MIXER");
    addAndMakeVisible (mixerLabel);

    const auto ampOf = [this] { return selectedChannel().getChildWithName (ids::INSTRUMENT)
                                                        .getChildWithName (ids::AMP); };
    const auto channelOf = [this] { return selectedChannel(); };

    basePitchSlider.setSliderStyle (juce::Slider::IncDecButtons);
    attachRotary (basePitchSlider, basePitchLabel, "PITCH", channelOf, ids::basePitch, 0, 127, 1,
                  "Change base pitch");

    attachRotary (attackSlider,  attackLabel,  "ATTACK",  ampOf, ids::attack,  0.0005, 2.0, 0.0005, "Change attack");
    attachRotary (decaySlider,   decayLabel,   "DECAY",   ampOf, ids::decay,   0.0005, 4.0, 0.0005, "Change decay");
    attachRotary (sustainSlider, sustainLabel, "SUSTAIN", ampOf, ids::sustain, 0.0,    1.0, 0.001,  "Change sustain");
    attachRotary (releaseSlider, releaseLabel, "RELEASE", ampOf, ids::release, 0.002,  4.0, 0.001,  "Change release");

    attachRotary (volumeSlider, volumeLabel, "VOLUME", channelOf, ids::volume, 0.0,  1.0, 0.001, "Change volume");
    attachRotary (panSlider,    panLabel,    "PAN",    channelOf, ids::pan,   -1.0,  1.0, 0.001, "Change pan");

    editorState.addChangeListener (this);
    document.getState().addListener (this);

    refresh();
}

InstrumentPanel::~InstrumentPanel()
{
    editorState.removeChangeListener (this);
    document.getState().removeListener (this);
}

void InstrumentPanel::attachRotary (juce::Slider& slider, juce::Label& label, const juce::String& text,
                                    std::function<juce::ValueTree()> owner,
                                    const juce::Identifier& property,
                                    double minimum, double maximum, double interval,
                                    const juce::String& transactionName)
{
    if (slider.getSliderStyle() != juce::Slider::IncDecButtons)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 58, tokens::size::controlHeightSm);
    }
    else
    {
        slider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 44, tokens::size::controlHeightSm);
    }

    slider.setRange (minimum, maximum, interval);

    // One transaction per gesture, so dragging a knob is a single undo step
    // rather than several hundred.
    slider.onDragStart = [this, transactionName]
    {
        dragging = true;
        document.getUndoManager().beginNewTransaction (transactionName);
    };

    slider.onDragEnd = [this] { dragging = false; };

    slider.onValueChange = [this, &slider, owner, property, transactionName]
    {
        if (updating)
            return;

        auto tree = owner();

        if (! tree.isValid())
            return;

        auto& undo = document.getUndoManager();

        // beginNewTransaction ARMS a new transaction rather than being a no-op
        // when one is open, so calling it per value change made every pixel of a
        // drag its own undo step - the thing the comment above claims it
        // prevents. During a drag the transaction opened at onDragStart is left
        // to coalesce; buttons and typed values produce no drag, so they open
        // their own.
        if (! dragging)
            undo.beginNewTransaction (transactionName);

        // Integer-valued properties must stay integers in the file: writing a
        // double would change the JSON from `0` to `0.0` and, worse, make the
        // schema's type coercion do the rounding instead of this code.
        if (slider.getInterval() >= 1.0)
            tree.setProperty (property, (int) slider.getValue(), &undo);
        else
            tree.setProperty (property, slider.getValue(), &undo);
    };

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
    oscSection.setOwner (channel.getChildWithName (ids::INSTRUMENT));
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
    g.fillAll (Palette::panel);
    g.setColour (Palette::line);
    g.drawVerticalLine (0, 0.0f, (float) getHeight());
}

void InstrumentPanel::resized()
{
    auto area = getLocalBounds().reduced (10);

    titleLabel.setBounds (area.removeFromTop (24));
    area.removeFromTop (6);

    const auto row = [&area] (int height) { auto r = area.removeFromTop (height); area.removeFromTop (6); return r; };

    oscSection.setBounds (row (OscillatorSection::requiredHeight));

    // Routing and base pitch share a row. The oscillator section costs the panel
    // about 120px more than the single wave combo it replaces, and at the
    // smallest window the app opens at that was the whole effect chain.
    auto routingRow = row (44);

    // The pitch stepper is measured from the right and the combo takes what is
    // left: an inc/dec pair given "half of whatever remains" is the one control
    // here that clips rather than shrinking.
    basePitchSlider.setBounds (routingRow.removeFromRight (96).reduced (0, 10));
    basePitchLabel.setBounds (routingRow.removeFromRight (38));
    routingRow.removeFromRight (8);
    mixerLabel.setBounds (routingRow.removeFromLeft (46));
    mixerBox.setBounds (routingRow.reduced (0, 10));

    auto adsr = row (86);
    const auto knobWidth = adsr.getWidth() / 4;

    const auto placeKnob = [] (juce::Rectangle<int> bounds, juce::Label& label, juce::Slider& slider)
    {
        label.setBounds (bounds.removeFromTop (14));
        slider.setBounds (bounds);
    };

    placeKnob (adsr.removeFromLeft (knobWidth), attackLabel, attackSlider);
    placeKnob (adsr.removeFromLeft (knobWidth), decayLabel, decaySlider);
    placeKnob (adsr.removeFromLeft (knobWidth), sustainLabel, sustainSlider);
    placeKnob (adsr, releaseLabel, releaseSlider);

    auto levels = row (86);
    placeKnob (levels.removeFromLeft (levels.getWidth() / 2), volumeLabel, volumeSlider);
    placeKnob (levels, panLabel, panSlider);

    chainHost.setBounds (area);
}

} // namespace dew
