#include "ui/ChannelRackComponent.h"

#include "model/Ids.h"
#include "model/Meter.h"
#include "model/ProjectEdits.h"
#include "ui/design/Icons.h"
#include "ui/design/Tokens.h"
#include "ui/primitives/DewControls.h"

namespace dew
{

using namespace tokens;

/** One channel's header: colour tab, name, volume, pan, mute and solo. */
class ChannelRackComponent::ChannelHeader : public juce::Component
{
public:
    ChannelHeader (ProjectDocument& d, EditorState& s, juce::ValueTree c)
        : document (d), editorState (s), channel (std::move (c))
    {
        // Named, so a test can find a row by asking rather than by counting the
        // widgets on it. It used to be identified as "one label and two
        // buttons", which stopped being true the moment a row gained a third.
        setComponentID ("channelHeader");

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
            // Selects on the CLICK. This was on onStateChange, which fires for
            // every internal transition - buttonNormal -> buttonOver among
            // them - and the row forwards its children's mouse events so it can
            // light up on hover. Between the two, merely moving the pointer
            // across M selected that channel with no click at all.
            select();

            auto& undo = document.getUndoManager();
            undo.beginNewTransaction ("Mute channel");
            channel.setProperty (ids::muted, muteButton.getToggleState(), &undo);
        };
        addAndMakeVisible (muteButton);

        soloButton.setToggleState ((bool) channel[ids::solo], juce::dontSendNotification);
        soloButton.onClick = [this]
        {
            select();

            auto& undo = document.getUndoManager();
            undo.beginNewTransaction ("Solo channel");
            channel.setProperty (ids::solo, soloButton.getToggleState(), &undo);
        };
        addAndMakeVisible (soloButton);

        // Volume and pan on the row itself, so a pattern can be balanced without
        // selecting each channel in turn and reaching for the instrument panel.
        // Same ranges as the panel's VOLUME and PAN, so the two read the same
        // number, and no caption fits on a 34px row - pan is told from volume by
        // filling out from the centre.
        attachKnob (volumeKnob, ids::volume, "Change volume", "Volume");
        attachKnob (panKnob, ids::pan, "Change pan", "Pan");
        panKnob.setBipolar (true);

        // Only audio channels can be armed, and only one channel at a time -
        // clicking an armed row's R disarms it rather than arming a second.
        armButton.setTooltip ("Arm this channel for recording");
        armButton.onClick = [this]
        {
            select();
            editorState.setArmedChannelId (armButton.getToggleState() ? getChannelId() : 0);
        };
        addChildComponent (armButton);

        // The knobs and the arm button keep their own clicks too, and select
        // the row from their own handlers - see attachKnob.

        // Hover only - see forwardChildMouseEventsTo.
        forwardChildMouseEventsTo (*this);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    int getChannelId() const  { return (int) channel[ids::id]; }

    void refresh()
    {
        // Guarded because this runs on every property change of this channel,
        // including the knob's own write: without it a drag would feed its value
        // back into the slider it came from.
        const juce::ScopedValueSetter<bool> quiet (updating, true);

        nameLabel.setText (channel[ids::name].toString(), juce::dontSendNotification);
        muteButton.setToggleState ((bool) channel[ids::muted], juce::dontSendNotification);
        soloButton.setToggleState ((bool) channel[ids::solo], juce::dontSendNotification);
        volumeKnob.setValue ((double) channel[ids::volume], juce::dontSendNotification);
        panKnob.setValue ((double) channel[ids::pan], juce::dontSendNotification);

        const auto audio = ProjectEdits::isAudioChannel (channel);
        armButton.setVisible (audio);
        armButton.setToggleState (audio && editorState.getArmedChannelId() == getChannelId(),
                                  juce::dontSendNotification);

        resized();
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
        // cannot drift into the mute and solo buttons. An audio channel has the
        // arm toggle in this slot instead: a recording has no base pitch, and a
        // number that means nothing is worse than no number.
        if (! ProjectEdits::isAudioChannel (channel))
        {
            g.setColour (colour::textDisabled);
            g.setFont (type::font (type::caption));
            g.drawText (juce::String ((int) channel[ids::basePitch]), pitchBounds,
                        juce::Justification::centredRight, false);
        }
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

        // The same slot the base pitch occupies, so the row's shape is the same
        // whichever kind of channel it is and the knobs never shift under the
        // cursor when a channel changes kind.
        armButton.setBounds (pitchBounds.withWidth (22).reduced (0, space::xxs));

        panKnob.setBounds (area.removeFromRight (size::knobSm));
        area.removeFromRight (space::xs);
        volumeKnob.setBounds (area.removeFromRight (size::knobSm));
        area.removeFromRight (space::xs);

        nameLabel.setBounds (area);
    }

private:
    ProjectDocument& document;
    EditorState& editorState;
    juce::ValueTree channel;

    /** Wires one compact knob to one of the channel's properties.

        `onEditStart` selects the row: a knob keeps its own clicks, so the row's
        mouseDown never sees them, and a control that cannot select its row is
        the bug this rack was already fixed for once.
    */
    void attachKnob (DewKnob& knob, const juce::Identifier& property,
                     const juce::String& transactionName, const juce::String& tooltip)
    {
        knob.setCompact (true);
        knob.setTooltip (tooltip);
        knob.setValue ((double) channel[property], juce::dontSendNotification);

        knob.onEditStart = [this, transactionName]
        {
            select();
            dragging = true;
            document.getUndoManager().beginNewTransaction (transactionName);
        };

        knob.onEditEnd = [this] { dragging = false; };

        knob.onValueChange = [this, &knob, property, transactionName]
        {
            if (updating)
                return;

            auto& undo = document.getUndoManager();

            // beginNewTransaction ARMS a new transaction rather than being a
            // no-op when one is open, so calling it per value change would make
            // every pixel of a drag its own undo step. During a drag the
            // transaction opened at onEditStart is left to coalesce; a wheel or
            // keyboard change produces no drag, so it opens its own.
            if (! dragging)
                undo.beginNewTransaction (transactionName);

            channel.setProperty (property, knob.getValue(), &undo);
        };

        addAndMakeVisible (knob);
    }

    void setHovered (bool shouldBeHovered)
    {
        if (std::exchange (hovered, shouldBeHovered) != shouldBeHovered)
            repaint();
    }

    juce::Label nameLabel;
    juce::Rectangle<int> pitchBounds;
    bool hovered = false;
    bool updating = false;
    bool dragging = false;
    DewLetterToggle muteButton { "M", colour::warning, "Mute this channel" };
    DewLetterToggle soloButton { "S", colour::success, "Solo this channel" };
    DewKnob volumeKnob { "VOL", 0.0, 1.0, 0.001 };
    DewKnob panKnob { "PAN", -1.0, 1.0, 0.001 };
    DewLetterToggle armButton { "R", colour::recording, "Arm this channel for recording" };
};

// -----------------------------------------------------------------------------

ChannelRackComponent::ChannelRackComponent (ProjectDocument& d, AudioEngine& e, EditorState& s, SamplePool* p)
    : document (d), engine (e), editorState (s), grid (d, e, s, p)
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
        const auto meter = Meter::of (document.getState());
        style.stepsPerBar = meter.stepsPerBar();
        style.beatsPerBar = meter.beatsPerBar;
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

    // The same span the piano roll writes, so the sequencer's ruler is no
    // longer the one place in the app where a loop can be seen but not chosen.
    ruler.onRangeChanged = [this] (juce::Range<int> steps)
    {
        editorState.setSelectedStepRange (steps);
        ruler.repaint();
    };

    ruler.onRangeCleared = [this]
    {
        editorState.clearStepSelection();
        ruler.repaint();
    };

    grid.onTimelineChanged = [this] { ruler.repaint(); };

    ruler.setComponentID ("channelRackRuler");
    addAndMakeVisible (ruler);

    contentHolder.addAndMakeVisible (grid);
    viewport.setViewedComponent (&contentHolder, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    addChannelButton.onClick = [this] { addChannel(); };
    addAudioButton.onClick = [this] { addAudioChannel(); };

    // Into the scrolling holder, not onto the panel: it is the next row of the
    // list, so it belongs to the list and scrolls with it.
    addChannelButton.setComponentID ("addChannelButton");
    contentHolder.addAndMakeVisible (addChannelButton);

    addAudioButton.setComponentID ("addAudioButton");
    addAudioButton.setTooltip ("Add a channel that plays a recording");
    contentHolder.addAndMakeVisible (addAudioButton);

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

void ChannelRackComponent::addAudioChannel()
{
    auto& undo = document.getUndoManager();
    undo.beginNewTransaction ("Add audio channel");
    const auto channel = ProjectEdits::addAudioChannel (document.getState(), {}, &undo);

    // Selected AND armed: adding an audio channel is something you do in order
    // to record into it, and leaving it disarmed makes Record answer with an
    // error message about a step the user has just taken.
    editorState.setSelectedChannelId ((int) channel[ids::id]);
    editorState.setArmedChannelId ((int) channel[ids::id]);
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

        // A freshly built row has to be brought up to date once, not only on
        // the next property change: everything a row shows CONDITIONALLY - the
        // arm toggle on an audio channel - is otherwise invisible until
        // something unrelated happens to touch the channel.
        header->refresh();
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
    else if (tree.hasType (ids::PROJECT))
    {
        // The rack had no branch for the project node at all, so a change to
        // the metre - or to the tempo, or to stepsPerBeat - left its bar
        // shading and its ruler drawn to the old grid until something else
        // happened to repaint them. resized() as well as a repaint, because the
        // ruler's Style is rebuilt during layout.
        resized();
        grid.repaint();
        repaint();
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
    // empty row of the list, where the channel it adds will appear. The two
    // kinds share that row rather than stacking, so the grid still starts one
    // row after the last channel.
    auto addRow = juce::Rectangle<int> (0, headers.size() * size::rowHeight,
                                        size::headerWidth, size::rowHeight)
                      .reduced (space::sm, space::xs);

    addChannelButton.setBounds (addRow.removeFromLeft (addRow.getWidth() / 2 - space::xxs));
    addAudioButton.setBounds (addRow.removeFromRight (addRow.getWidth() - space::xs));

    grid.setBounds (size::headerWidth, 0,
                    juce::jmax (120, contentHolder.getWidth() - size::headerWidth),
                    contentHeight);
}

} // namespace dew
