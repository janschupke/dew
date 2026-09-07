#include "ui/AudioSettingsPanel.h"
#include "i18n/Strings.h"
#include "ui/DewDialog.h"

#include "ui/primitives/DewMeter.h"
#include "ui/design/Tokens.h"

namespace dew
{

using namespace tokens;

AudioSettingsPanel::AudioSettingsPanel (LiveAudioHost& host, AudioEngine& e)
    : audioHost (host)
    , deviceManager (host.getDeviceManager())
    , engine (e)
{
    setComponentID ("audioSettings");
    // A name and a PLACE in the tree a screen reader is given. focusContainer,
    // not keyboardFocusContainer: the two flags are independent, and the second
    // would confine the tab key to this panel with no key to leave it - a
    // keyboard trap, which is worse than the flat tab order it would tidy.
    setTitle (tr (StringId::audio_title));
    setFocusContainerType (FocusContainerType::focusContainer);

    setSize (preferredWidth, preferredHeight);

    labels = { "DRIVER", "OUTPUT", "INPUT", "CHANNELS", "RATE", "BUFFER" };

    for (auto* box : { &typeBox, &outputBox, &inputBox, &inputChannelBox, &rateBox, &bufferBox })
        addAndMakeVisible (box);

    inputChannelBox.addItem (tr (StringId::audio_input_monoLeft), 1);
    inputChannelBox.addItem (tr (StringId::audio_input_monoRight), 2);
    inputChannelBox.addItem (tr (StringId::audio_input_stereo), 3);
    inputChannelBox.setSelectedId (1, juce::dontSendNotification);
    inputChannelBox.setEnabled (false);

    inputChannelBox.onChange = [this]
    {
        if (updating)
            return;

        auto setup = deviceManager.getAudioDeviceSetup();

        // Explicit channels, so "mono - right" is the right-hand input rather
        // than whichever one the device happens to call first.
        setup.useDefaultInputChannels = false;
        setup.inputChannels.clear();

        switch (inputChannelBox.getSelectedId())
        {
            case 1: setup.inputChannels.setBit (0); break;
            case 2: setup.inputChannels.setBit (1); break;
            default:
                setup.inputChannels.setBit (0);
                setup.inputChannels.setBit (1);
                break;
        }

        applySetup (setup);
    };

    // 30 Hz, the rate the design tokens declare for a UI refresh.
    startTimerHz (motion::uiRefreshHz);

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

        // The moment the microphone is actually asked for. Doing it here rather
        // than at startup is the whole reason dew opens output-only; a refusal
        // comes back as a message rather than as a meter that never moves.
        if (const auto error = audioHost.setInputEnabled (! none); error.isNotEmpty())
        {
            summaryText = error;
            repaint();
        }

        inputChannelBox.setEnabled (! none);
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

    testButton.setTooltip (tr (StringId::audio_test_help));
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
          setup.outputDeviceName, tr (StringId::audio_output_systemDefault));
    fill (inputBox, type != nullptr ? type->getDeviceNames (true) : juce::StringArray(),
          setup.inputDeviceName, tr (StringId::audio_input_none));

    rateBox.clear (juce::dontSendNotification);
    bufferBox.clear (juce::dontSendNotification);

    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        for (const auto rate : device->getAvailableSampleRates())
        {
            const auto asInt = juce::roundToInt (rate);
            rateBox.addItem (tr (StringId::unit_hertzValue, Args {}.with ("value", asInt)), asInt);
        }

        rateBox.setSelectedId (juce::roundToInt (device->getCurrentSampleRate()),
                               juce::dontSendNotification);

        for (const auto size : device->getAvailableBufferSizes())
            bufferBox.addItem (tr (StringId::unit_samplesValue, Args {}.with ("value", size)),
                               size);

        bufferBox.setSelectedId (device->getCurrentBufferSizeSamples(), juce::dontSendNotification);
    }

    const auto haveDevice = deviceManager.getCurrentAudioDevice() != nullptr;
    rateBox.setEnabled (haveDevice);
    bufferBox.setEnabled (haveDevice);
    testButton.setEnabled (haveDevice);

    // Which input channels to take is meaningless with no input selected, and
    // an enabled control that does nothing is worse than a disabled one. Set
    // here as well as in the input handler, or the panel opens with it live.
    inputChannelBox.setEnabled (haveDevice && isInputSelected());

    updateSummary();
    repaint();
}

void AudioSettingsPanel::updateSummary()
{
    auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
    {
        summaryText = tr (StringId::audio_summary_closed);
        return;
    }

    const auto rate = device->getCurrentSampleRate();
    const auto block = device->getCurrentBufferSizeSamples();
    const auto latencyMs = rate > 0.0 ? (double) block / rate * 1000.0 : 0.0;

    // Latency in milliseconds, because that is the number a buffer size is
    // actually chosen for.
    summaryText = tr (StringId::audio_summary_open,
                      Args {}
                          .with ("rate", juce::roundToInt (rate))
                          .with ("samples", block)
                          .with ("latency", juce::String (latencyMs, 1)));
}

void AudioSettingsPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    rebuildLists();
}

void AudioSettingsPanel::timerCallback()
{
    const auto peak = audioHost.getRecorder().readAndClearInputPeak();

    inputLevel = meter::fall (inputLevel, peak, 1000 / motion::uiRefreshHz);

    repaint (meterArea.expanded (space::xxs));
}

void AudioSettingsPanel::resized()
{
    auto area = contentBounds();
    labelBounds.clearQuick();

    juce::ComboBox* boxes[] { &typeBox,         &outputBox, &inputBox,
                              &inputChannelBox, &rateBox,   &bufferBox };

    for (auto* box : boxes)
    {
        auto row = area.removeFromTop (size::controlHeight);
        labelBounds.add (row.removeFromLeft (size::gutterLabel));
        row.removeFromLeft (space::md);
        box->setBounds (row);
        area.removeFromTop (space::md);
    }

    area.removeFromTop (space::md);

    // Under the last combo, spanning the same column the boxes occupy so it
    // reads as belonging to the input rows above it.
    //
    // Taken from the area the loop above left behind, rather than recomputed
    // from the row height and the row count: the meter used to state that
    // arithmetic a second time, in a const method called from paint(), and
    // adding a seventh row would have moved the boxes and left the meter.
    meterArea = area.withHeight (size::meterHeight).withTrimmedLeft (size::gutterLabel + space::md);

    auto footer = area;
    layOutFooter (footer, { &testButton });
}

void AudioSettingsPanel::paint (juce::Graphics& g)
{
    paintBackground (g);

    const auto content = contentBounds();

    g.setFont (type::font (type::caption));
    g.setColour (colour::textSecondary);

    for (int i = 0; i < labelBounds.size() && i < labels.size(); ++i)
        g.drawText (labels[i], labelBounds[i], juce::Justification::centredLeft, false);

    // The input meter. Drawn whether or not an input is open, because an empty
    // meter beside a chosen input is information - it says the device is there
    // and silent, which is different from there being no meter at all.
    const auto meter = meterArea;

    g.setColour (colour::wellDeep);
    g.fillRoundedRectangle (meter.toFloat(), radius::sm);

    if (isInputSelected())
    {
        // Scaled in dB like the mixer's meters. Linear - which this was - puts
        // a healthy input in the bottom fifth of the bar and reads as broken.
        const auto proportion = meter::proportionForGain (inputLevel);
        const auto filled = meter.toFloat().withWidth (meter.toFloat().getWidth() * proportion);

        // Red at the top of the scale rather than a gradient across it: what a
        // recording meter has to say is "this is about to clip", and that is a
        // threshold, not a slope.
        g.setColour (proportion > meter::hotProportion ? colour::danger : colour::success);
        g.fillRoundedRectangle (filled, radius::sm);
    }

    // Where the meter stops being green, said by POSITION as well as by hue.
    // The bar's height already carries the level; the threshold it crosses was
    // carried by the colour change alone, and a colour change is the one cue
    // some readers do not get.
    {
        const auto bar = meter.toFloat();
        const auto x = bar.getX() + bar.getWidth() * meter::hotProportion;

        g.setColour (colour::dividerStrong);
        g.fillRect (x, bar.getY(), stroke::hairline, bar.getHeight());
    }

    g.setColour (colour::outline);
    g.drawRoundedRectangle (meter.toFloat(), radius::sm, stroke::hairline);

    g.setFont (type::font (type::caption));
    g.setColour (colour::textSecondary);
    // Beside the bar rather than at a fixed 16 from the panel's own edge: that
    // spelling of the inset stopped being true the moment this panel could be
    // laid out inside somebody else's pane.
    g.drawText (tr (StringId::audio_inputLevel),
                meter.withX (content.getX()).withWidth (size::gutterLabel),
                juce::Justification::centredLeft, false);

    // The live state, under the controls: what the choices above added up to.
    const auto summary = content.withTop (content.getBottom() - size::controlHeight * 2 - space::md)
                             .withHeight (size::controlHeight);

    const auto open = deviceManager.getCurrentAudioDevice() != nullptr;

    g.setColour (open ? colour::success : colour::danger);
    g.fillEllipse ((float) summary.getX(), (float) summary.getCentreY() - 3.0f, 6.0f, 6.0f);

    g.setColour (open ? colour::textPrimary : colour::danger);
    g.setFont (type::font (type::small));
    g.drawText (summaryText, summary.withTrimmedLeft (space::lg), juce::Justification::centredLeft,
                true);
}

void AudioSettingsPanel::show (LiveAudioHost& host, AudioEngine& engine, juce::Component* parent)
{
    dialog::launch (std::make_unique<AudioSettingsPanel> (host, engine), tr (StringId::audio_title),
                    parent);
}

} // namespace dew
