#include "MainComponent.h"

#include "../model/Ids.h"

namespace dew
{

MainComponent::MainComponent (bool openAudioDevice)
    : audioHost (engine),
      transportBar (document, engine, editorState),
      tabs (document, engine, editorState),
      instrumentPanel (document, editorState)
{
    juce::Desktop::getInstance().setDefaultLookAndFeel (&lookAndFeel);

    addAndMakeVisible (transportBar);
    addAndMakeVisible (tabs);
    addAndMakeVisible (instrumentPanel);

    // Any document change schedules a snapshot rebuild. Coalescing through the
    // AsyncUpdater means a knob drag costs one rebuild per message-loop turn
    // rather than one per mouse move.
    document.onProjectChanged = [this] { triggerAsyncUpdate(); };

    // Push the initial project before opening the device, so the first block
    // the engine renders already has something in it.
    projectChanged();

    if (! openAudioDevice)
    {
        deviceStatus = "audio device not opened";
    }
    else if (const auto error = audioHost.start(); error.isNotEmpty())
    {
        deviceStatus = "Audio unavailable: " + error;
    }
    else
    {
        deviceStatus = audioHost.describeDevice();
    }

    transportBar.setStatusText (deviceStatus);

    setSize (1180, 760);
}

MainComponent::~MainComponent()
{
    cancelPendingUpdate();
    audioHost.stop();
    juce::Desktop::getInstance().setDefaultLookAndFeel (nullptr);
}

void MainComponent::handleAsyncUpdate()
{
    projectChanged();
}

void MainComponent::projectChanged()
{
    juce::StringArray warnings;
    engine.setProject (document.getState(), &warnings);

    // A project that cannot be rendered as the user expects is worth saying so
    // once, in the status line, rather than silently playing something else.
    if (! warnings.isEmpty())
        transportBar.setStatusText (warnings[0]);
    else
        transportBar.setStatusText (deviceStatus);
}

void MainComponent::documentWasReplaced()
{
    document.onProjectChanged = [this] { triggerAsyncUpdate(); };

    transportBar.refresh();
    tabs.refresh();
    instrumentPanel.refresh();

    engine.stop();
    engine.rewind();
    projectChanged();
}

void MainComponent::flushPendingEngineUpdate()
{
    handleUpdateNowIfNeeded();
}

void MainComponent::showLoadWarnings (const juce::StringArray& warnings)
{
    if (warnings.isEmpty())
        return;

    transportBar.setStatusText (juce::String (warnings.size()) + " item"
                                + (warnings.size() == 1 ? "" : "s")
                                + " in this file were not understood: " + warnings[0]);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (Palette::background);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    transportBar.setBounds (area.removeFromTop (46));
    instrumentPanel.setBounds (area.removeFromRight (300));
    tabs.setBounds (area);
}

} // namespace dew
