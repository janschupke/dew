#include "MixerComponent.h"

#include "../model/ProjectEdits.h"
#include "design/Tokens.h"

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

        // The fader took the strip's whole remaining height and the label its
        // top, so selection was reachable only through a 6px border. The label
        // becomes inert and renaming moves to a double-click on the strip.
        nameLabel.setInterceptsMouseClicks (false, false);
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
        gainSlider.onDragStart = [this]
        {
            select();
            document.getUndoManager().beginNewTransaction ("Change level");
        };
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
            panSlider.onDragStart = [this]
            {
                select();
                document.getUndoManager().beginNewTransaction ("Change pan");
            };
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
                select();
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
                select();
                auto& undo = document.getUndoManager();
                undo.beginNewTransaction ("Solo");
                track.setProperty (ids::solo, soloButton.getToggleState(), &undo);
            };
            addAndMakeVisible (soloButton);
        }

        forwardChildMouseEventsTo (*this);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);

        track.addListener (this);
    }

    ~Strip() override
    {
        track.removeListener (this);
    }

    void setSelected (bool shouldBeSelected)
    {
        if (std::exchange (selected, shouldBeSelected) != shouldBeSelected)
            repaint();
    }

    bool isMasterStrip() const noexcept { return isMaster; }
    int getTrackId() const { return (int) track[ids::id]; }

    std::function<void()> onSelected;

    void select()
    {
        if (onSelected != nullptr)
            onSelected();
    }

    void mouseDoubleClick (const juce::MouseEvent& event) override
    {
        if (! isMaster && nameLabel.getBounds().contains (event.getPosition()))
            nameLabel.showEditor();
    }

    void mouseEnter (const juce::MouseEvent&) override { setHovered (true); }
    void mouseExit (const juce::MouseEvent&) override  { setHovered (isMouseOver (true)); }

    void mouseDown (const juce::MouseEvent&) override
    {
        select();
    }

    void paint (juce::Graphics& g) override
    {
        const auto body = getLocalBounds().toFloat().reduced (2.0f);

        g.setColour (selected ? tokens::colour::surfaceRaised
                              : hovered ? tokens::colour::surface.brighter (0.06f)
                                        : tokens::colour::surface);
        g.fillRoundedRectangle (body, tokens::radius::md);

        // Master gets a neutral outline rather than an accent one: now that it
        // is selectable, an accent border on it always would read as selected.
        if (isMaster || selected)
        {
            g.setColour (selected ? tokens::colour::accent : tokens::colour::outline);
            g.drawRoundedRectangle (body, tokens::radius::md,
                                    selected ? tokens::stroke::regular : tokens::stroke::hairline);
        }

        // A cap along the top edge, so which strip is selected is readable from
        // across the mixer rather than from a few percent of brightness.
        if (selected)
        {
            g.setColour (tokens::colour::accent);
            g.fillRoundedRectangle (body.withHeight (3.0f), 1.5f);
        }

        // How many effects the strip carries, so it says what it holds without
        // having to be selected first. Top corner rather than the bottom, which
        // is where the fader's value box already is.
        const auto effectCount = ProjectEdits::countEffects (track);

        if (effectCount > 0)
        {
            const auto badge = juce::Rectangle<float> ((float) getWidth() - 22.0f, 5.0f, 16.0f, 12.0f);

            g.setColour (tokens::colour::accent);
            g.fillRoundedRectangle (badge, 3.0f);

            g.setColour (tokens::colour::textOnAccent);
            g.setFont (tokens::type::font (tokens::type::caption, true));
            g.drawText (juce::String (effectCount), badge.toNearestInt(),
                        juce::Justification::centred, false);
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
    void setHovered (bool shouldBeHovered)
    {
        if (std::exchange (hovered, shouldBeHovered) != shouldBeHovered)
            repaint();
    }

    bool isMaster;
    bool selected = false;
    bool hovered = false;

    juce::Label nameLabel;
    juce::Slider gainSlider;
    juce::Slider panSlider;
    juce::TextButton muteButton, soloButton;
};

// -----------------------------------------------------------------------------

MixerComponent::MixerComponent (ProjectDocument& d, EditorState& s)
    : document (d), editorState (s), effectChain (d)
{
    setComponentID ("mixer");

    addAndMakeVisible (effectChain);
    editorState.addChangeListener (this);

    document.getState().addListener (this);
    rebuildStrips();
}

MixerComponent::~MixerComponent()
{
    editorState.removeChangeListener (this);
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
        {
            auto* strip = strips.add (new Strip (document, track, false));
            const auto id = (int) track[ids::id];
            strip->onSelected = [this, id] { editorState.setSelectedMixerTrackId (id); };
        }

    // Master had no onSelected at all, so clicking it did nothing and its chain
    // could never be edited. It selects like any other strip now.
    if (const auto master = mixer.getChildWithName (ids::MASTER); master.isValid())
    {
        auto* strip = strips.add (new Strip (document, master, true));
        strip->onSelected = [this] { editorState.setSelectedMixerTrackId (masterTrackId); };
    }

    for (auto* strip : strips)
        addAndMakeVisible (strip);

    pointChainAtSelectedTrack();
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

void MixerComponent::pointChainAtSelectedTrack()
{
    const auto selectedId = editorState.getSelectedMixerTrackId();
    juce::ValueTree selectedTrack;

    for (const auto& track : document.getState().getChildWithName (ids::MIXER))
        if (track.hasType (ids::MIXER_TRACK) && (int) track[ids::id] == selectedId)
            selectedTrack = track;

    effectChain.setOwner (selectedTrack);

    for (auto* strip : strips)
        strip->setSelected (strip->isMasterStrip() ? selectedId == masterTrackId
                                                   : strip->getTrackId() == selectedId);
}

void MixerComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    pointChainAtSelectedTrack();
}

void MixerComponent::resized()
{
    auto area = getLocalBounds().reduced (8);

    // The chain editor gets the bottom of the panel, at a fixed height: strips
    // need the rest and a fader is useless once it is shorter than a thumb.
    auto chainArea = area.removeFromBottom (juce::jmin (chainHeight, area.getHeight() / 2));

    // Capped rather than stretched: a number field wider than a hand is not
    // easier to drag, only emptier.
    effectChain.setBounds (chainArea.withTrimmedTop (tokens::space::md)
                                    .withWidth (juce::jmin (chainWidth, chainArea.getWidth())));

    for (auto* strip : strips)
        strip->setBounds (area.removeFromLeft (stripWidth));
}

} // namespace dew
