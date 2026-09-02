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
    : document (d), editorState (s), effectChain (d, s)
{
    setComponentID ("instrumentPanel");

    chainViewport.setViewedComponent (&effectChain, false);
    chainViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (chainViewport);

    effectChain.onRequiredHeightChanged = [this] { layOutChain(); };

    titleLabel.setFont (tokens::type::font (tokens::type::title, true));
    titleLabel.setColour (juce::Label::textColourId, Palette::text);
    addAndMakeVisible (titleLabel);

    waveBox.addItem ("Sine", 1);
    waveBox.addItem ("Saw", 2);
    waveBox.addItem ("Square", 3);
    waveBox.addItem ("Triangle", 4);
    waveBox.onChange = [this]
    {
        if (updating)
            return;

        auto channel = selectedChannel();

        if (! channel.isValid())
            return;

        static const char* waves[] = { "sine", "saw", "square", "triangle" };
        const auto index = juce::jlimit (0, 3, waveBox.getSelectedId() - 1);

        auto& undo = document.getUndoManager();
        undo.beginNewTransaction ("Change waveform");
        channel.getChildWithName (ids::INSTRUMENT)
               .getChildWithName (ids::OSC)
               .setProperty (ids::wave, waves[index], &undo);
    };
    addAndMakeVisible (waveBox);
    styleCaption (waveLabel, "WAVE");
    addAndMakeVisible (waveLabel);

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

    const auto oscOf = [this] { return selectedChannel().getChildWithName (ids::INSTRUMENT)
                                                        .getChildWithName (ids::OSC); };
    const auto ampOf = [this] { return selectedChannel().getChildWithName (ids::INSTRUMENT)
                                                        .getChildWithName (ids::AMP); };
    const auto channelOf = [this] { return selectedChannel(); };

    octaveSlider.setSliderStyle (juce::Slider::IncDecButtons);
    attachRotary (octaveSlider, octaveLabel, "OCT", oscOf, ids::octave, -3, 3, 1, "Change octave");

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
        document.getUndoManager().beginNewTransaction (transactionName);
    };

    slider.onValueChange = [this, &slider, owner, property, transactionName]
    {
        if (updating)
            return;

        auto tree = owner();

        if (! tree.isValid())
            return;

        auto& undo = document.getUndoManager();

        // Buttons and typed values produce no drag, so there may be no
        // transaction open; beginNewTransaction is a no-op if one already is.
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
    if (tree.hasType (ids::CHANNEL) || tree.hasType (ids::OSC) || tree.hasType (ids::AMP)
        || tree.hasType (ids::MIXER_TRACK))
        refresh();
}

void InstrumentPanel::refresh()
{
    const juce::ScopedValueSetter<bool> quiet (updating, true);

    const auto channel = selectedChannel();
    const auto valid = channel.isValid();

    setEnabled (valid);
    effectChain.setOwner (channel);
    effectChain.setOwnerName (channel.isValid() ? channel[ids::name].toString() : juce::String());

    if (! valid)
    {
        titleLabel.setText ("No channel selected", juce::dontSendNotification);
        return;
    }

    titleLabel.setText (channel[ids::name].toString(), juce::dontSendNotification);

    const auto osc = channel.getChildWithName (ids::INSTRUMENT).getChildWithName (ids::OSC);
    const auto amp = channel.getChildWithName (ids::INSTRUMENT).getChildWithName (ids::AMP);

    const auto wave = osc[ids::wave].toString();
    waveBox.setSelectedId (wave == "sine" ? 1 : wave == "saw" ? 2 : wave == "square" ? 3 : 4,
                           juce::dontSendNotification);

    mixerBox.clear (juce::dontSendNotification);

    for (const auto& track : document.getState().getChildWithName (ids::MIXER))
        if (track.hasType (ids::MIXER_TRACK))
            mixerBox.addItem (track[ids::name].toString(), (int) track[ids::id]);

    mixerBox.setSelectedId ((int) channel[ids::mixerTrackId], juce::dontSendNotification);

    octaveSlider.setValue ((double) osc[ids::octave], juce::dontSendNotification);
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

    auto waveRow = row (44);
    waveLabel.setBounds (waveRow.removeFromLeft (46));
    waveBox.setBounds (waveRow.reduced (0, 10));

    auto mixerRow = row (44);
    mixerLabel.setBounds (mixerRow.removeFromLeft (46));
    mixerBox.setBounds (mixerRow.reduced (0, 10));

    auto pitchRow = row (30);
    octaveLabel.setBounds (pitchRow.removeFromLeft (36));
    octaveSlider.setBounds (pitchRow.removeFromLeft (96));
    pitchRow.removeFromLeft (8);
    basePitchLabel.setBounds (pitchRow.removeFromLeft (42));
    basePitchSlider.setBounds (pitchRow);

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

    chainViewport.setBounds (area);
    layOutChain();
}

void InstrumentPanel::layOutChain()
{
    // The chain is as tall as it needs to be; the viewport scrolls it.
    const auto width = juce::jmax (120, chainViewport.getMaximumVisibleWidth());
    effectChain.setSize (width, juce::jmax (chainViewport.getHeight(),
                                            effectChain.getRequiredHeight()));
}

} // namespace dew
