#include "ChannelRackComponent.h"

#include "../model/Ids.h"
#include "../model/ProjectEdits.h"
#include "DewLookAndFeel.h"

namespace dew
{

/** One channel's header: colour tab, name, mute, and selection. */
class ChannelRackComponent::ChannelHeader : public juce::Component
{
public:
    ChannelHeader (ProjectDocument& d, EditorState& s, juce::ValueTree c)
        : document (d), editorState (s), channel (std::move (c))
    {
        nameLabel.setText (channel[ids::name].toString(), juce::dontSendNotification);
        nameLabel.setEditable (false, true, false);
        nameLabel.setFont (juce::FontOptions (13.0f));
        nameLabel.onTextChange = [this]
        {
            auto& undo = document.getUndoManager();
            undo.beginNewTransaction ("Rename channel");
            channel.setProperty (ids::name, nameLabel.getText(), &undo);
        };
        addAndMakeVisible (nameLabel);

        muteButton.setButtonText ("M");
        muteButton.setClickingTogglesState (true);
        muteButton.setToggleState ((bool) channel[ids::muted], juce::dontSendNotification);
        muteButton.onClick = [this]
        {
            auto& undo = document.getUndoManager();
            undo.beginNewTransaction ("Mute channel");
            channel.setProperty (ids::muted, muteButton.getToggleState(), &undo);
        };
        addAndMakeVisible (muteButton);
    }

    int getChannelId() const  { return (int) channel[ids::id]; }

    void refresh()
    {
        nameLabel.setText (channel[ids::name].toString(), juce::dontSendNotification);
        muteButton.setToggleState ((bool) channel[ids::muted], juce::dontSendNotification);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto selected = editorState.getSelectedChannelId() == getChannelId();

        g.fillAll (selected ? Palette::panel.brighter (0.08f) : Palette::panel);

        const auto colour = juce::Colour::fromString ("ff" + channel[ids::colour].toString().getLastCharacters (6));
        g.setColour (colour);
        g.fillRect (0, 0, 4, getHeight());

        g.setColour (Palette::line);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

        if (selected)
        {
            g.setColour (Palette::accent);
            g.drawRect (getLocalBounds(), 1);
        }
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        editorState.setSelectedChannelId (getChannelId());
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (6, 3);
        area.removeFromLeft (4);
        muteButton.setBounds (area.removeFromRight (26).reduced (0, 2));
        area.removeFromRight (4);
        nameLabel.setBounds (area);
    }

private:
    ProjectDocument& document;
    EditorState& editorState;
    juce::ValueTree channel;
    juce::Label nameLabel;
    juce::TextButton muteButton;
};

// -----------------------------------------------------------------------------

ChannelRackComponent::ChannelRackComponent (ProjectDocument& d, AudioEngine& e, EditorState& s)
    : document (d), engine (e), editorState (s), grid (d, e, s)
{
    setComponentID ("channelRack");
    contentHolder.setComponentID ("channelRackContent");
    viewport.setComponentID ("channelRackViewport");

    juce::ignoreUnused (engine);

    contentHolder.addAndMakeVisible (grid);
    viewport.setViewedComponent (&contentHolder, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    addChannelButton.onClick = [this]
    {
        auto& undo = document.getUndoManager();
        undo.beginNewTransaction ("Add channel");
        const auto channel = ProjectEdits::addChannel (document.getState(), {}, &undo);
        editorState.setSelectedChannelId ((int) channel[ids::id]);
    };
    addAndMakeVisible (addChannelButton);

    document.getState().addListener (this);
    editorState.addChangeListener (this);

    rebuildHeaders();
}

ChannelRackComponent::~ChannelRackComponent()
{
    document.getState().removeListener (this);
    editorState.removeChangeListener (this);
}

void ChannelRackComponent::refresh()
{
    document.getState().addListener (this);
    rebuildHeaders();
}

void ChannelRackComponent::rebuildHeaders()
{
    headers.clear();

    for (const auto& channel : document.getState())
        if (channel.hasType (ids::CHANNEL))
            headers.add (new ChannelHeader (document, editorState, channel));

    for (auto* header : headers)
        contentHolder.addAndMakeVisible (header);

    resized();
    repaint();
}

void ChannelRackComponent::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child)
{
    if (child.hasType (ids::CHANNEL))
        rebuildHeaders();
    else if (child.hasType (ids::NOTE))
        grid.repaint();
}

void ChannelRackComponent::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& child, int)
{
    if (child.hasType (ids::CHANNEL))
        rebuildHeaders();
    else if (child.hasType (ids::NOTE))
        grid.repaint();
}

void ChannelRackComponent::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    if (tree.hasType (ids::CHANNEL))
    {
        for (auto* header : headers)
            if (header->getChannelId() == (int) tree[ids::id])
                header->refresh();

        if (property == ids::basePitch || property == ids::colour)
            grid.repaint();
    }
    else if (tree.hasType (ids::PATTERN) || tree.hasType (ids::NOTE))
    {
        grid.repaint();
    }
}

void ChannelRackComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    for (auto* header : headers)
        header->repaint();

    grid.repaint();
}

void ChannelRackComponent::paint (juce::Graphics& g)
{
    g.fillAll (Palette::background);
}

void ChannelRackComponent::resized()
{
    auto area = getLocalBounds();

    auto footer = area.removeFromBottom (34).reduced (8, 4);
    addChannelButton.setBounds (footer.removeFromLeft (100));

    viewport.setBounds (area);

    const auto contentHeight = juce::jmax (area.getHeight(),
                                           headers.size() * StepGridComponent::rowHeight);

    contentHolder.setSize (viewport.getMaximumVisibleWidth(), contentHeight);

    for (int i = 0; i < headers.size(); ++i)
        headers[i]->setBounds (0, i * StepGridComponent::rowHeight,
                               headerWidth, StepGridComponent::rowHeight);

    grid.setBounds (headerWidth, 0,
                    juce::jmax (100, contentHolder.getWidth() - headerWidth),
                    juce::jmax (StepGridComponent::rowHeight,
                                headers.size() * StepGridComponent::rowHeight));
}

} // namespace dew
