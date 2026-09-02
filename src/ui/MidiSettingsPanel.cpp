#include "ui/MidiSettingsPanel.h"

#include "ui/design/Tokens.h"

namespace dew
{

using namespace tokens;

namespace
{
juce::String transposeLabel (int semitones)
{
    if (semitones == 0)
        return "None";

    const auto sign = semitones > 0 ? "+" : "";
    return juce::String (sign) + juce::String (semitones) + " semitones";
}
} // namespace

// -----------------------------------------------------------------------------

MidiSettingsPanel::DeviceRow::DeviceRow (MidiSettingsPanel& o, juce::MidiDeviceInfo i, bool present)
    : owner (o), info (std::move (i)), connected (present)
{
    tick.setToggleState (owner.host.isDeviceWanted (info.identifier), juce::dontSendNotification);
    tick.onClick = [this]
    {
        if (owner.updating)
            return;

        owner.host.setDeviceEnabled (info.identifier, tick.getToggleState());
        owner.updateSummary();
        owner.repaint();

        if (owner.onDevicesChanged != nullptr)
            owner.onDevicesChanged();
    };

    addAndMakeVisible (tick);
}

void MidiSettingsPanel::DeviceRow::resized()
{
    tick.setBounds (getLocalBounds().removeFromLeft (size::iconButton));
}

void MidiSettingsPanel::DeviceRow::paint (juce::Graphics& g)
{
    auto text = getLocalBounds().withTrimmedLeft (size::iconButton + space::sm);

    g.setFont (type::font (type::body));
    g.setColour (connected ? colour::textPrimary : colour::textDisabled);
    g.drawText (info.name.isNotEmpty() ? info.name : info.identifier,
                text, juce::Justification::centredLeft, true);

    if (connected)
        return;

    // The cable is out. Say so rather than dropping the row, so the tick that
    // is still set has an explanation beside it.
    g.setFont (type::font (type::caption));
    g.setColour (colour::warning);
    g.drawText ("not connected", text, juce::Justification::centredRight, false);
}

// -----------------------------------------------------------------------------

MidiSettingsPanel::MidiSettingsPanel (MidiInputHost& h, Settings* s)
    : host (h), settings (s)
{
    setComponentID ("midiSettings");
    setSize (preferredWidth, preferredHeight);

    labels = { "CHANNEL", "TRANSPOSE" };

    listViewport.setViewedComponent (&listHolder, false);
    listViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (listViewport);

    channelBox.addItem ("Omni (all channels)", 1);

    for (int channel = 1; channel <= 16; ++channel)
        channelBox.addItem ("Channel " + juce::String (channel), channel + 1);

    channelBox.onChange = [this]
    {
        if (updating)
            return;

        const auto filter = channelBox.getSelectedId() - 1;
        host.getRouter().setChannelFilter (filter);

        if (settings != nullptr)
            settings->setMidiChannelFilter (filter);
    };

    // Ids are offset so that "no transpose" is not id 0, which a ComboBox uses
    // to mean nothing selected.
    for (int semitones = -MidiRouter::maxTranspose; semitones <= MidiRouter::maxTranspose; ++semitones)
        transposeBox.addItem (transposeLabel (semitones), semitones + MidiRouter::maxTranspose + 1);

    transposeBox.onChange = [this]
    {
        if (updating)
            return;

        const auto semitones = transposeBox.getSelectedId() - MidiRouter::maxTranspose - 1;
        host.getRouter().setTranspose (semitones);

        if (settings != nullptr)
            settings->setMidiTranspose (semitones);
    };

    addAndMakeVisible (channelBox);
    addAndMakeVisible (transposeBox);

    host.onDevicesChanged = [this] { refresh(); };

    refresh();
    startTimerHz ((int) motion::uiRefreshHz);
}

MidiSettingsPanel::~MidiSettingsPanel()
{
    host.onDevicesChanged = nullptr;
}

void MidiSettingsPanel::refresh()
{
    const juce::ScopedValueSetter<bool> quiet (updating, true);

    rows.clearQuick (true);

    const auto present = MidiInputHost::getAvailableDevices();

    for (const auto& device : host.getListedDevices())
    {
        const auto connected = std::any_of (present.begin(), present.end(),
                                            [&] (const auto& d) { return d.identifier == device.identifier; });

        auto* row = rows.add (new DeviceRow (*this, device, connected));
        listHolder.addAndMakeVisible (row);
    }

    const auto filter = host.getRouter().getChannelFilter();
    channelBox.setSelectedId (filter + 1, juce::dontSendNotification);

    const auto semitones = host.getRouter().getTranspose();
    transposeBox.setSelectedId (semitones + MidiRouter::maxTranspose + 1, juce::dontSendNotification);

    resized();
    updateSummary();
    repaint();
}

void MidiSettingsPanel::updateSummary()
{
    // The list above already says when it is empty; repeating it here would
    // spend the one live line in the panel on something already on screen.
    if (rows.isEmpty())
    {
        summaryText = "Connect a controller to play.";
        return;
    }

    if (host.getNumWantedDevices() == 0)
    {
        summaryText = "Tick an input to play it.";
        return;
    }

    if (! host.hasConnectedInput())
    {
        summaryText = host.describeInputs();
        return;
    }

    const auto packed = host.getRouter().getLastNote();

    if (packed < 0)
    {
        summaryText = host.describeInputs() + "  -  waiting";
        return;
    }

    const auto pitch = packed & 0xff;
    const auto velocity = (packed >> 8) & 0xff;
    const auto channel = (packed >> 16) & 0xff;

    summaryText = "Note " + juce::String (pitch)
                + "  ·  vel " + juce::String (velocity)
                + "  ·  ch " + juce::String (channel);
}

void MidiSettingsPanel::timerCallback()
{
    // Poll the router's counters rather than have the MIDI thread reach into a
    // component. Nothing crosses a thread boundary but an atomic load.
    const auto activity = host.getRouter().getActivityCount();

    if (activity == lastActivity)
        return;

    lastActivity = activity;
    updateSummary();
    repaint();
}

juce::String MidiSettingsPanel::getDeviceRowText (int index) const
{
    if (auto* row = rows[index])
        return (row->info.name.isNotEmpty() ? row->info.name : row->info.identifier)
             + (row->connected ? juce::String() : juce::String (" (not connected)"));

    return {};
}

bool MidiSettingsPanel::isDeviceRowTicked (int index) const
{
    if (auto* row = rows[index])
        return row->tick.getToggleState();

    return false;
}

void MidiSettingsPanel::resized()
{
    auto area = getLocalBounds().reduced (space::xl);

    // The heading sits above the list; paint() draws it into this strip.
    area.removeFromTop (size::controlHeightSm + space::xs);

    listViewport.setBounds (area.removeFromTop (listHeight));
    listHolder.setSize (juce::jmax (0, listViewport.getWidth() - size::iconButton),
                        juce::jmax (1, rows.size() * size::stripFormRow));

    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, i * size::stripFormRow, listHolder.getWidth(), size::stripFormRow);

    area.removeFromTop (space::lg);

    labelBounds.clearQuick();

    for (auto* box : { &channelBox, &transposeBox })
    {
        auto row = area.removeFromTop (size::controlHeight);
        labelBounds.add (row.removeFromLeft (size::gutterLabel));
        row.removeFromLeft (space::md);
        box->setBounds (row);
        area.removeFromTop (space::md);
    }
}

void MidiSettingsPanel::paint (juce::Graphics& g)
{
    g.fillAll (colour::background);

    auto area = getLocalBounds().reduced (space::xl);

    paint::sectionHeading (g, area.removeFromTop (size::controlHeightSm), "INPUTS");

    paint::wellBackground (g, listViewport.getBounds());

    if (rows.isEmpty())
        paint::emptyState (g, listViewport.getBounds(), "No MIDI inputs were found");

    g.setFont (type::font (type::caption));
    g.setColour (colour::textSecondary);

    for (int i = 0; i < labelBounds.size() && i < labels.size(); ++i)
        g.drawText (labels[i], labelBounds[i], juce::Justification::centredLeft, false);

    // The live state, under the controls: what the choices above added up to.
    const auto summary = getLocalBounds().reduced (space::xl)
                             .withTop (getHeight() - space::xl - size::controlHeight)
                             .withHeight (size::controlHeight);

    // Three states, not two: nothing asked for, asked for but not here, and
    // actually listening. Green for the last of those only.
    const auto connected = host.hasConnectedInput();
    const auto wanted = host.getNumWantedDevices() > 0;

    const auto light = connected ? colour::success
                                 : (wanted ? colour::warning : colour::textDisabled);

    g.setColour (light);
    g.fillEllipse ((float) summary.getX(), (float) summary.getCentreY() - 3.0f, 6.0f, 6.0f);

    g.setColour (connected ? colour::textPrimary : colour::textSecondary);
    g.setFont (type::font (type::small));
    g.drawText (summaryText, summary.withTrimmedLeft (space::lg),
                juce::Justification::centredLeft, true);
}

void MidiSettingsPanel::show (MidiInputHost& hostToUse, Settings* settingsToUse, juce::Component* parent)
{
    juce::DialogWindow::LaunchOptions options;

    options.content.setOwned (new MidiSettingsPanel (hostToUse, settingsToUse));
    options.dialogTitle = "MIDI Settings";
    options.dialogBackgroundColour = colour::background;
    options.componentToCentreAround = parent;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;

    options.launchAsync();
}

} // namespace dew
