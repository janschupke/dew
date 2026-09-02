#include "MixerComponent.h"

#include "../model/Ids.h"
#include "DewLookAndFeel.h"

namespace dew
{

/** One mixer strip. Master has no pan, mute or solo, so it is the same class
    with those controls hidden rather than a second nearly-identical one.
*/
class MixerComponent::Strip : public juce::Component,
                              private juce::ValueTree::Listener
{
public:
    Strip (ProjectDocument& d, juce::ValueTree t, bool isMasterStrip)
        : document (d), track (std::move (t)), isMaster (isMasterStrip)
    {
        nameLabel.setText (isMaster ? "Master" : track[ids::name].toString(),
                           juce::dontSendNotification);
        nameLabel.setJustificationType (juce::Justification::centred);
        nameLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        nameLabel.setEditable (false, ! isMaster, false);
        nameLabel.onTextChange = [this]
        {
            auto& undo = document.getUndoManager();
            undo.beginNewTransaction ("Rename mixer track");
            track.setProperty (ids::name, nameLabel.getText(), &undo);
        };
        addAndMakeVisible (nameLabel);

        gainSlider.setSliderStyle (juce::Slider::LinearVertical);
        gainSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 16);
        gainSlider.setRange (0.0, 1.5, 0.001);
        gainSlider.setValue ((double) track[ids::gain], juce::dontSendNotification);
        gainSlider.onDragStart = [this] { document.getUndoManager().beginNewTransaction ("Change level"); };
        gainSlider.onValueChange = [this]
        {
            auto& undo = document.getUndoManager();
            undo.beginNewTransaction ("Change level");
            track.setProperty (ids::gain, gainSlider.getValue(), &undo);
        };
        addAndMakeVisible (gainSlider);

        if (! isMaster)
        {
            panSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            panSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            panSlider.setRange (-1.0, 1.0, 0.001);
            panSlider.setValue ((double) track[ids::pan], juce::dontSendNotification);
            panSlider.onDragStart = [this] { document.getUndoManager().beginNewTransaction ("Change pan"); };
            panSlider.onValueChange = [this]
            {
                auto& undo = document.getUndoManager();
                undo.beginNewTransaction ("Change pan");
                track.setProperty (ids::pan, panSlider.getValue(), &undo);
            };
            addAndMakeVisible (panSlider);

            muteButton.setButtonText ("M");
            muteButton.setClickingTogglesState (true);
            muteButton.setToggleState ((bool) track[ids::mute], juce::dontSendNotification);
            muteButton.onClick = [this]
            {
                auto& undo = document.getUndoManager();
                undo.beginNewTransaction ("Mute");
                track.setProperty (ids::mute, muteButton.getToggleState(), &undo);
            };
            addAndMakeVisible (muteButton);

            soloButton.setButtonText ("S");
            soloButton.setClickingTogglesState (true);
            soloButton.setToggleState ((bool) track[ids::solo], juce::dontSendNotification);
            soloButton.onClick = [this]
            {
                auto& undo = document.getUndoManager();
                undo.beginNewTransaction ("Solo");
                track.setProperty (ids::solo, soloButton.getToggleState(), &undo);
            };
            addAndMakeVisible (soloButton);
        }

        track.addListener (this);
    }

    ~Strip() override
    {
        track.removeListener (this);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (isMaster ? Palette::panel.brighter (0.05f) : Palette::panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 4.0f);

        if (isMaster)
        {
            g.setColour (Palette::accent.withAlpha (0.5f));
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 4.0f, 1.0f);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (6, 8);

        nameLabel.setBounds (area.removeFromTop (18));
        area.removeFromTop (4);

        if (! isMaster)
        {
            panSlider.setBounds (area.removeFromTop (40).reduced (8, 0));
            area.removeFromTop (4);

            auto buttons = area.removeFromTop (22);
            muteButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (1));
            soloButton.setBounds (buttons.reduced (1));
            area.removeFromTop (4);
        }

        gainSlider.setBounds (area);
    }

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& property) override
    {
        if (property == ids::gain)
            gainSlider.setValue ((double) track[ids::gain], juce::dontSendNotification);
        else if (property == ids::pan)
            panSlider.setValue ((double) track[ids::pan], juce::dontSendNotification);
        else if (property == ids::mute)
            muteButton.setToggleState ((bool) track[ids::mute], juce::dontSendNotification);
        else if (property == ids::solo)
            soloButton.setToggleState ((bool) track[ids::solo], juce::dontSendNotification);
        else if (property == ids::name)
            nameLabel.setText (track[ids::name].toString(), juce::dontSendNotification);
    }

    ProjectDocument& document;
    juce::ValueTree track;
    bool isMaster;

    juce::Label nameLabel;
    juce::Slider gainSlider;
    juce::Slider panSlider;
    juce::TextButton muteButton, soloButton;
};

// -----------------------------------------------------------------------------

MixerComponent::MixerComponent (ProjectDocument& d, EditorState& s)
    : document (d), editorState (s)
{
    setComponentID ("mixer");

    juce::ignoreUnused (editorState);

    document.getState().addListener (this);
    rebuildStrips();
}

MixerComponent::~MixerComponent()
{
    document.getState().removeListener (this);
}

void MixerComponent::refresh()
{
    document.getState().addListener (this);
    rebuildStrips();
}

void MixerComponent::rebuildStrips()
{
    strips.clear();

    const auto mixer = document.getState().getChildWithName (ids::MIXER);

    for (const auto& track : mixer)
        if (track.hasType (ids::MIXER_TRACK))
            strips.add (new Strip (document, track, false));

    if (const auto master = mixer.getChildWithName (ids::MASTER); master.isValid())
        strips.add (new Strip (document, master, true));

    for (auto* strip : strips)
        addAndMakeVisible (strip);

    resized();
    repaint();
}

void MixerComponent::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child)
{
    if (child.hasType (ids::MIXER_TRACK))
        rebuildStrips();
}

void MixerComponent::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& child, int)
{
    if (child.hasType (ids::MIXER_TRACK))
        rebuildStrips();
}

void MixerComponent::paint (juce::Graphics& g)
{
    g.fillAll (Palette::background);
}

void MixerComponent::resized()
{
    auto area = getLocalBounds().reduced (8);

    for (auto* strip : strips)
        strip->setBounds (area.removeFromLeft (stripWidth));
}

} // namespace dew
