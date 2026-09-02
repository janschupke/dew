#include "ChannelRackComponent.h"

#include "../model/Ids.h"
#include "../model/ProjectEdits.h"
#include "design/Icons.h"
#include "design/Tokens.h"
#include "primitives/DewControls.h"

namespace dew
{

using namespace tokens;

/** One channel's header: colour tab, name, mute and solo. */
class ChannelRackComponent::ChannelHeader : public juce::Component
{
public:
    ChannelHeader (ProjectDocument& d, EditorState& s, juce::ValueTree c)
        : document (d), editorState (s), channel (std::move (c))
    {
        nameLabel.setText (channel[ids::name].toString(), juce::dontSendNotification);
        nameLabel.setEditable (false, true, false);

        // The label covered the row's whole left half and consumed every press,
        // so clicking a channel by its name selected nothing. Renaming moves to
        // a double-click on the row, which is where it already was.
        nameLabel.setInterceptsMouseClicks (false, false);
        nameLabel.setFont (type::font (type::body));
        nameLabel.setColour (juce::Label::textColourId, colour::textPrimary);
        nameLabel.onTextChange = [this]
        {
            auto& undo = document.getUndoManager();
            undo.beginNewTransaction ("Rename channel");
            channel.setProperty (ids::name, nameLabel.getText(), &undo);
        };
        addAndMakeVisible (nameLabel);

        muteButton.setToggleState ((bool) channel[ids::muted], juce::dontSendNotification);
        muteButton.onClick = [this]
        {
            auto& undo = document.getUndoManager();
            undo.beginNewTransaction ("Mute channel");
            channel.setProperty (ids::muted, muteButton.getToggleState(), &undo);
        };
        addAndMakeVisible (muteButton);

        soloButton.setToggleState ((bool) channel[ids::solo], juce::dontSendNotification);
        soloButton.onClick = [this]
        {
            auto& undo = document.getUndoManager();
            undo.beginNewTransaction ("Solo channel");
            channel.setProperty (ids::solo, soloButton.getToggleState(), &undo);
        };
        addAndMakeVisible (soloButton);

        // M and S must keep their clicks, so they select the row explicitly.
        muteButton.onStateChange = [this] { select(); };
        soloButton.onStateChange = [this] { select(); };

        // Hover only - see forwardChildMouseEventsTo.
        forwardChildMouseEventsTo (*this);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    int getChannelId() const  { return (int) channel[ids::id]; }

    void refresh()
    {
        nameLabel.setText (channel[ids::name].toString(), juce::dontSendNotification);
        muteButton.setToggleState ((bool) channel[ids::muted], juce::dontSendNotification);
        soloButton.setToggleState ((bool) channel[ids::solo], juce::dontSendNotification);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto selected = editorState.getSelectedChannelId() == getChannelId();

        g.setColour (selected ? colour::surfaceHover
                              : hovered ? colour::surfaceRaised : colour::surface);
        g.fillAll();

        const auto colourValue = juce::Colour::fromString (
            "ff" + channel[ids::colour].toString().getLastCharacters (6));

        g.setColour (colourValue);
        g.fillRect (0, 0, 4, getHeight());

        g.setColour (colour::divider);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

        if (selected)
        {
            g.setColour (colour::accent);
            g.fillRect (0, 0, 4, getHeight());
            g.drawRect (getLocalBounds(), 1);
        }

        // The base pitch, so a melodic channel says what it is playing. Its
        // bounds come from resized() rather than being recomputed here, so it
        // cannot drift into the mute and solo buttons.
        g.setColour (colour::textDisabled);
        g.setFont (type::font (type::caption));
        g.drawText (juce::String ((int) channel[ids::basePitch]), pitchBounds,
                    juce::Justification::centredRight, false);
    }

    void select() { editorState.setSelectedChannelId (getChannelId()); }

    void mouseDown (const juce::MouseEvent&) override { select(); }

    void mouseDoubleClick (const juce::MouseEvent& event) override
    {
        if (nameLabel.getBounds().contains (event.getPosition()))
            nameLabel.showEditor();
    }

    void mouseEnter (const juce::MouseEvent&) override { setHovered (true); }
    void mouseExit (const juce::MouseEvent&) override  { setHovered (! isMouseOver (true)); }

    void resized() override
    {
        auto area = getLocalBounds().reduced (space::sm, space::xs);
        area.removeFromLeft (space::xs);

        soloButton.setBounds (area.removeFromRight (22).reduced (0, space::xxs));
        area.removeFromRight (space::xxs);
        muteButton.setBounds (area.removeFromRight (22).reduced (0, space::xxs));

        area.removeFromRight (space::sm);
        pitchBounds = area.removeFromRight (26);
        area.removeFromRight (space::xs);

        nameLabel.setBounds (area);
    }

private:
    ProjectDocument& document;
    EditorState& editorState;
    juce::ValueTree channel;

    void setHovered (bool shouldBeHovered)
    {
        if (std::exchange (hovered, shouldBeHovered) != shouldBeHovered)
            repaint();
    }

    juce::Label nameLabel;
    juce::Rectangle<int> pitchBounds;
    bool hovered = false;
    DewLetterToggle muteButton { "M", colour::warning, "Mute this channel" };
    DewLetterToggle soloButton { "S", colour::success, "Solo this channel" };
};

// -----------------------------------------------------------------------------

ChannelRackComponent::ChannelRackComponent (ProjectDocument& d, AudioEngine& e, EditorState& s)
    : document (d), engine (e), editorState (s), grid (d, e, s)
{
    setComponentID ("channelRack");
    contentHolder.setComponentID ("channelRackContent");
    viewport.setComponentID ("channelRackViewport");

    // The channel rack had no ruler at all: no bar numbers, and nothing to
    // click to move the transport. It reads the grid's timeline rather than
    // keeping a copy, so the two cannot drift apart.
    ruler.timelineSource = [this]() -> const TimelineView& { return grid.getTimeline(); };

    ruler.styleSource = [this]
    {
        ruler::Style style;
        style.stepsPerBar = juce::jmax (1, (int) document.getState()[ids::stepsPerBeat]) * 4;
        style.totalSteps = grid.getNumSteps();
        style.playing = engine.isPlaying();

        if (engine.getMode() == Transport::Mode::pattern)
            style.playheadSteps = (double) ((int) engine.getPlayheadSteps() % style.totalSteps);

        return style;
    };

    ruler.onSeek = [this] (double steps) { engine.setPlayheadSteps (steps); };

    grid.onTimelineChanged = [this] { ruler.repaint(); };

    ruler.setComponentID ("channelRackRuler");
    addAndMakeVisible (ruler);

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

    removeChannelButton.onClick = [this]
    {
        auto channel = ProjectEdits::findChannel (document.getState(),
                                                  editorState.getSelectedChannelId());

        if (! channel.isValid())
            return;

        auto& undo = document.getUndoManager();
        undo.beginNewTransaction ("Remove channel");
        ProjectEdits::removeChannel (document.getState(), channel, &undo);
    };
    addAndMakeVisible (removeChannelButton);

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

void ChannelRackComponent::valueTreePropertyChanged (juce::ValueTree& tree,
                                                     const juce::Identifier& property)
{
    if (tree.hasType (ids::CHANNEL))
    {
        for (auto* header : headers)
            if (header->getChannelId() == (int) tree[ids::id])
                header->refresh();

        grid.repaint();
    }
    else if (tree.hasType (ids::PATTERN) || tree.hasType (ids::NOTE))
    {
        if (property == ids::lengthSteps)
            resized();

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
    g.fillAll (colour::background);

    // The header column continues below the last channel so the split between
    // names and steps stays readable down the whole panel.
    g.setColour (colour::surface.withAlpha (0.4f));
    g.fillRect (0, 0, size::headerWidth, getHeight() - footerHeight);

    g.setColour (colour::dividerStrong);
    g.drawVerticalLine (size::headerWidth, 0.0f, (float) (getHeight() - footerHeight));
}

void ChannelRackComponent::resized()
{
    auto area = getLocalBounds();

    auto footer = area.removeFromBottom (footerHeight).reduced (space::md, space::sm);
    addChannelButton.setBounds (footer.removeFromLeft (104).withHeight (size::controlHeight));
    footer.removeFromLeft (space::sm);
    removeChannelButton.setBounds (footer.removeFromLeft (104).withHeight (size::controlHeight));

    // The ruler spans the step columns only; the header column keeps its own
    // corner, which the shared ruler knows nothing about.
    ruler.setBounds (area.removeFromTop (size::rulerHeight).withTrimmedLeft (size::headerWidth));

    viewport.setBounds (area);

    // Content is at least as tall as the viewport, so the grid always fills the
    // panel and can paint the region below the rows as inert.
    const auto rowsHeight = headers.size() * size::rowHeight;
    const auto visibleHeight = viewport.getMaximumVisibleHeight();
    const auto contentHeight = juce::jmax (visibleHeight, rowsHeight);

    contentHolder.setSize (viewport.getMaximumVisibleWidth(), contentHeight);

    for (int i = 0; i < headers.size(); ++i)
        headers[i]->setBounds (0, i * size::rowHeight, size::headerWidth, size::rowHeight);

    grid.setBounds (size::headerWidth, 0,
                    juce::jmax (120, contentHolder.getWidth() - size::headerWidth),
                    contentHeight);
}

} // namespace dew
