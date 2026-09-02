#include "AudioSettingsPanel.h"

#include "design/Tokens.h"

namespace dew
{

using namespace tokens;

AudioSettingsPanel::AudioSettingsPanel (LiveAudioHost& host, AudioEngine& e)
    : deviceManager (host.getDeviceManager()), engine (e)
{
    setComponentID ("audioSettings");
    setSize (preferredWidth, preferredHeight);

    labels = { "DRIVER", "OUTPUT", "INPUT", "RATE", "BUFFER" };

    for (auto* box : { &typeBox, &outputBox, &inputBox, &rateBox, &bufferBox })
        addAndMakeVisible (box);

    typeBox.onChange = [this]
    {
        if (updating)
            return;

        deviceManager.setCurrentAudioDeviceType (typeBox.getText(), true);
        rebuildLists();
    };

    outputBox.onChange = [this]
    {
        if (updating)
            return;

        auto setup = deviceManager.getAudioDeviceSetup();
        setup.outputDeviceName = outputBox.getSelectedId() == 1 ? juce::String()
                                                                : outputBox.getText();
        setup.useDefaultOutputChannels = true;
        applySetup (setup);
    };

    inputBox.onChange = [this]
    {
        if (updating)
            return;

        auto setup = deviceManager.getAudioDeviceSetup();
        const auto none = inputBox.getSelectedId() == 1;

        setup.inputDeviceName = none ? juce::String() : inputBox.getText();
        setup.useDefaultInputChannels = ! none;

        if (none)
            setup.inputChannels.clear();

        applySetup (setup);
    };

    rateBox.onChange = [this]
    {
        if (updating)
            return;

        auto setup = deviceManager.getAudioDeviceSetup();
        setup.sampleRate = (double) rateBox.getSelectedId();
        applySetup (setup);
    };

    bufferBox.onChange = [this]
    {
        if (updating)
            return;

        auto setup = deviceManager.getAudioDeviceSetup();
        setup.bufferSize = bufferBox.getSelectedId();
        applySetup (setup);
    };

    testButton.setTooltip ("Play a short note through the current device");
    testButton.onClick = [this]
    {
        // Through the same preview path the piano roll's keys use, so this
        // tests the whole chain rather than a tone generator beside it.
        engine.previewNoteOn (0, 69, 0.6f);
        juce::Timer::callAfterDelay (450, [this] { engine.previewAllOff(); });
    };
    addAndMakeVisible (testButton);

    deviceManager.addChangeListener (this);
    rebuildLists();
}

AudioSettingsPanel::~AudioSettingsPanel()
{
    deviceManager.removeChangeListener (this);
}

void AudioSettingsPanel::applySetup (const juce::AudioDeviceManager::AudioDeviceSetup& setup)
{
    // treatAsChosenDevice, so the choice is what gets persisted rather than
    // being treated as a fallback the manager may quietly replace.
    if (const auto error = deviceManager.setAudioDeviceSetup (setup, true); error.isNotEmpty())
        summaryText = error;

    rebuildLists();

    if (onDeviceChanged != nullptr)
        onDeviceChanged();
}

void AudioSettingsPanel::rebuildLists()
{
    const juce::ScopedValueSetter<bool> quiet (updating, true);

    typeBox.clear (juce::dontSendNotification);

    const auto& types = deviceManager.getAvailableDeviceTypes();

    for (int i = 0; i < types.size(); ++i)
    {
        typeBox.addItem (types[i]->getTypeName(), i + 1);

        if (types[i]->getTypeName() == deviceManager.getCurrentAudioDeviceType())
            typeBox.setSelectedId (i + 1, juce::dontSendNotification);
    }

    auto* type = deviceManager.getCurrentDeviceTypeObject();
    const auto setup = deviceManager.getAudioDeviceSetup();

    const auto fill = [] (juce::ComboBox& box, const juce::StringArray& names,
                          const juce::String& selected, const juce::String& noneLabel)
    {
        box.clear (juce::dontSendNotification);
        box.addItem (noneLabel, 1);

        for (int i = 0; i < names.size(); ++i)
            box.addItem (names[i], i + 2);

        const auto index = names.indexOf (selected);
        box.setSelectedId (index >= 0 ? index + 2 : 1, juce::dontSendNotification);
    };

    fill (outputBox, type != nullptr ? type->getDeviceNames (false) : juce::StringArray(),
          setup.outputDeviceName, "System default");
    fill (inputBox, type != nullptr ? type->getDeviceNames (true) : juce::StringArray(),
          setup.inputDeviceName, "None");

    rateBox.clear (juce::dontSendNotification);
    bufferBox.clear (juce::dontSendNotification);

    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        for (const auto rate : device->getAvailableSampleRates())
        {
            const auto asInt = juce::roundToInt (rate);
            rateBox.addItem (juce::String (asInt) + " Hz", asInt);
        }

        rateBox.setSelectedId (juce::roundToInt (device->getCurrentSampleRate()),
                               juce::dontSendNotification);

        for (const auto size : device->getAvailableBufferSizes())
            bufferBox.addItem (juce::String (size) + " samples", size);

        bufferBox.setSelectedId (device->getCurrentBufferSizeSamples(), juce::dontSendNotification);
    }

    const auto haveDevice = deviceManager.getCurrentAudioDevice() != nullptr;
    rateBox.setEnabled (haveDevice);
    bufferBox.setEnabled (haveDevice);
    testButton.setEnabled (haveDevice);

    updateSummary();
    repaint();
}

void AudioSettingsPanel::updateSummary()
{
    auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
    {
        summaryText = "No audio device is open.";
        return;
    }

    const auto rate = device->getCurrentSampleRate();
    const auto block = device->getCurrentBufferSizeSamples();
    const auto latencyMs = rate > 0.0 ? (double) block / rate * 1000.0 : 0.0;

    // Latency in milliseconds, because that is the number a buffer size is
    // actually chosen for.
    summaryText = juce::String (juce::roundToInt (rate)) + " Hz  -  "
                + juce::String (block) + " samples  -  "
                + juce::String (latencyMs, 1) + " ms";
}

void AudioSettingsPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    rebuildLists();
}

void AudioSettingsPanel::resized()
{
    auto area = getLocalBounds().reduced (space::xl);
    labelBounds.clearQuick();

    juce::ComboBox* boxes[] { &typeBox, &outputBox, &inputBox, &rateBox, &bufferBox };

    for (auto* box : boxes)
    {
        auto row = area.removeFromTop (size::controlHeight);
        labelBounds.add (row.removeFromLeft (66));
        row.removeFromLeft (space::md);
        box->setBounds (row);
        area.removeFromTop (space::md);
    }

    area.removeFromTop (space::md);
    testButton.setBounds (area.removeFromBottom (size::controlHeight).removeFromRight (110));
}

void AudioSettingsPanel::paint (juce::Graphics& g)
{
    g.fillAll (colour::background);

    g.setFont (type::font (type::caption));
    g.setColour (colour::textSecondary);

    for (int i = 0; i < labelBounds.size() && i < labels.size(); ++i)
        g.drawText (labels[i], labelBounds[i], juce::Justification::centredLeft, false);

    // The live state, under the controls: what the choices above added up to.
    const auto summary = getLocalBounds().reduced (space::xl)
                             .withTop (getHeight() - space::xl - size::controlHeight * 2 - space::md)
                             .withHeight (size::controlHeight);

    const auto open = deviceManager.getCurrentAudioDevice() != nullptr;

    g.setColour (open ? colour::success : colour::danger);
    g.fillEllipse ((float) summary.getX(), (float) summary.getCentreY() - 3.0f, 6.0f, 6.0f);

    g.setColour (open ? colour::textPrimary : colour::danger);
    g.setFont (type::font (type::small));
    g.drawText (summaryText, summary.withTrimmedLeft (space::lg),
                juce::Justification::centredLeft, true);
}

void AudioSettingsPanel::show (LiveAudioHost& host, AudioEngine& engine, juce::Component* parent)
{
    juce::DialogWindow::LaunchOptions options;

    options.content.setOwned (new AudioSettingsPanel (host, engine));
    options.dialogTitle = "Audio Settings";
    options.dialogBackgroundColour = colour::background;
    options.componentToCentreAround = parent;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;

    options.launchAsync();
}

} // namespace dew
