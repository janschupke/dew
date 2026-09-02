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

    /** What the row's context menu offers, and what each item does.

        Built and applied as named methods rather than as a lambda inside
        showMenuAsync, because showMenuAsync cannot be driven headlessly and
        every other gesture in this app is tested that way. The menu is then
        only the way a person reaches these; a test reaches them directly.
    */
    enum class MenuItem { rename = 1, addChannel, removeChannel };

    juce::PopupMenu buildMenu() const
    {
        juce::PopupMenu menu;
        menu.addItem ((int) MenuItem::rename, "Rename");
        menu.addItem ((int) MenuItem::addChannel, "Add channel");
        menu.addSeparator();
        menu.addItem ((int) MenuItem::removeChannel, "Remove channel");
        return menu;
    }

    void applyMenuChoice (int choice)
    {
        switch ((MenuItem) choice)
        {
            case MenuItem::rename:         nameLabel.showEditor(); break;
            case MenuItem::addChannel:     if (onAddChannel) onAddChannel(); break;
            case MenuItem::removeChannel:  if (onRemoveChannel) onRemoveChannel (getChannelId()); break;
            default: break;
        }
    }

    /** Add and remove belong to the rack, which owns the list and rebuilds it. */
    std::function<void()> onAddChannel;
    std::function<void (int channelId)> onRemoveChannel;

    void mouseDown (const juce::MouseEvent& event) override
    {
        // Selected first, so the menu always acts on the row that was clicked
        // rather than on whatever happened to be selected before it.
        select();

        if (! event.mods.isPopupMenu())
            return;

        auto menu = buildMenu();

        // The look and feel has to be set explicitly or the popup overrides in
        // DewLookAndFeel do not apply - and anchored at the POINTER rather than
        // at a component, which is the first menu in the app to do so: a row is
        // not a button, and a menu covering the row you just aimed at is worse
        // than one beside the cursor.
        menu.setLookAndFeel (&getLookAndFeel());
        menu.showMenuAsync (juce::PopupMenu::Options()
                                .withTargetScreenArea ({ event.getScreenX(), event.getScreenY(), 1, 1 }),
                            [safe = juce::Component::SafePointer<ChannelHeader> (this)] (int choice)
                            {
                                if (safe != nullptr && choice > 0)
                                    safe->applyMenuChoice (choice);
                            });
    }

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

        // The same span the piano roll selects: both are views of one pattern,
        // so a loop taken out in one has to be visible in the other.
        if (editorState.hasStepSelection())
        {
            const auto selection = editorState.getSelectedStepRange();
            style.selectionStartSteps = (double) selection.getStart();
            style.selectionEndSteps = (double) selection.getEnd();
        }

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

    addChannelButton.onClick = [this] { addChannel(); };

    // Into the scrolling holder, not onto the panel: it is the next row of the
    // list, so it belongs to the list and scrolls with it.
    addChannelButton.setComponentID ("addChannelButton");
    contentHolder.addAndMakeVisible (addChannelButton);

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

void ChannelRackComponent::addChannel()
{
    auto& undo = document.getUndoManager();
    undo.beginNewTransaction ("Add channel");
    const auto channel = ProjectEdits::addChannel (document.getState(), {}, &undo);
    editorState.setSelectedChannelId ((int) channel[ids::id]);
}

void ChannelRackComponent::removeChannel (int channelId)
{
    auto channel = ProjectEdits::findChannel (document.getState(), channelId);

    if (! channel.isValid())
        return;

    auto& undo = document.getUndoManager();
    undo.beginNewTransaction ("Remove channel");
    ProjectEdits::removeChannel (document.getState(), channel, &undo);
}

bool ChannelRackComponent::applyChannelMenuChoice (int channelId, int choice)
{
    for (auto* header : headers)
        if (header->getChannelId() == channelId)
        {
            header->applyMenuChoice (choice);
            return true;
        }

    return false;
}

juce::StringArray ChannelRackComponent::channelMenuItems (int channelId) const
{
    for (const auto* header : headers)
        if (header->getChannelId() == channelId)
        {
            juce::StringArray items;

            // Held in a named local: MenuItemIterator keeps a REFERENCE, so
            // iterating a temporary menu walks a destroyed object and silently
            // yields nothing.
            const auto menu = header->buildMenu();

            for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
                items.add (it.getItem().isSeparator ? "-" : it.getItem().text);

            return items;
        }

    return {};
}

void ChannelRackComponent::rebuildHeaders()
{
    headers.clear();

    for (const auto& channel : document.getState())
        if (channel.hasType (ids::CHANNEL))
            headers.add (new ChannelHeader (document, editorState, channel));

    for (auto* header : headers)
    {
        header->onAddChannel = [this] { addChannel(); };
        header->onRemoveChannel = [this] (int id) { removeChannel (id); };
        contentHolder.addAndMakeVisible (header);
    }

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
    // names and steps stays readable down the whole panel. It runs the full
    // height now that the add button is a row of the list rather than a footer.
    g.setColour (colour::surface.withAlpha (0.4f));
    g.fillRect (0, 0, size::headerWidth, getHeight());

    g.setColour (colour::dividerStrong);
    g.drawVerticalLine (size::headerWidth, 0.0f, (float) getHeight());
}

void ChannelRackComponent::resized()
{
    auto area = getLocalBounds();

    // The ruler spans the step columns only; the header column keeps its own
    // corner, which the shared ruler knows nothing about.
    ruler.setBounds (area.removeFromTop (size::rulerHeight).withTrimmedLeft (size::headerWidth));

    viewport.setBounds (area);

    // Content is at least as tall as the viewport, so the grid always fills the
    // panel and can paint the region below the rows as inert. The add button is
    // counted as a row of its own, or it would be unreachable the moment the
    // channels overflow the viewport.
    const auto rowsHeight = (headers.size() + 1) * size::rowHeight;
    const auto visibleHeight = viewport.getMaximumVisibleHeight();
    const auto contentHeight = juce::jmax (visibleHeight, rowsHeight);

    contentHolder.setSize (viewport.getMaximumVisibleWidth(), contentHeight);

    for (int i = 0; i < headers.size(); ++i)
        headers[i]->setBounds (0, i * size::rowHeight, size::headerWidth, size::rowHeight);

    // Directly below the last channel, spanning the header column: the next
    // empty row of the list, where the channel it adds will appear.
    addChannelButton.setBounds (juce::Rectangle<int> (0, headers.size() * size::rowHeight,
                                                      size::headerWidth, size::rowHeight)
                                    .reduced (space::sm, space::xs));

    grid.setBounds (size::headerWidth, 0,
                    juce::jmax (120, contentHolder.getWidth() - size::headerWidth),
                    contentHeight);
}

} // namespace dew
