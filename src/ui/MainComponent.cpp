#include "MainComponent.h"

#include "AudioSettingsPanel.h"
#include "design/Tokens.h"

#include "../model/Ids.h"

namespace dew
{

MainComponent::MainComponent (bool openAudioDevice)
    : audioHost (engine),
      transportBar (document, engine, editorState),
      tabs (document, engine, editorState),
      instrumentPanel (document, editorState),
      statusBar (document, editorState, audioHost)
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

    addAndMakeVisible (statusBar);
    addAndMakeVisible (divider);

    if (! openAudioDevice)
    {
        statusBar.showMessage ("Audio device not opened", StatusBar::Severity::warning);
    }
    else if (const auto error = audioHost.start(); error.isNotEmpty())
    {
        statusBar.showMessage ("Audio unavailable: " + error, StatusBar::Severity::error);
    }
    else
    {
        statusBar.showMessage (audioHost.describeDevice(), StatusBar::Severity::info);
    }

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
    // once rather than silently playing something else. It expires on its own
    // now instead of standing until something overwrites it.
    if (! warnings.isEmpty())
        statusBar.showMessage (warnings[0], StatusBar::Severity::warning);
}

void MainComponent::documentWasReplaced()
{
    document.onProjectChanged = [this] { triggerAsyncUpdate(); };

    transportBar.refresh();
    tabs.refresh();
    instrumentPanel.refresh();
    statusBar.refresh();

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

    statusBar.showMessage (juce::String (warnings.size()) + " item"
                           + (warnings.size() == 1 ? "" : "s")
                           + " in this file were not understood: " + warnings[0],
                           StatusBar::Severity::warning);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (Palette::background);
}

void MainComponent::PanelDivider::paint (juce::Graphics& g)
{
    g.fillAll (tokens::colour::background);

    g.setColour (isMouseOverOrDragging() ? tokens::colour::accent : tokens::colour::dividerStrong);
    g.drawVerticalLine (getWidth() / 2, 0.0f, (float) getHeight());
}

void MainComponent::setPanelWidth (int width)
{
    const auto clamped = juce::jlimit (Settings::minPanelWidth, Settings::maxPanelWidth, width);

    if (clamped == panelWidth)
        return;

    panelWidth = clamped;
    resized();
}

void MainComponent::applySettings (const Settings& settings)
{
    panelWidth = settings.getPanelWidth();

    editorState.setSelectedChannelId (settings.getSelectedChannelId());
    editorState.setSelectedMixerTrackId (settings.getSelectedMixerTrackId());
    editorState.setCurrentPatternId (settings.getCurrentPatternId());

    tabs.setCurrentTabIndex (settings.getTabIndex(), false);
    tabs.applyPianoRollView (settings.getPianoRollZoom(), settings.getPianoRollScroll(),
                             settings.getPianoRollPitchScroll());
    tabs.setPianoRollSnap (settings.getPianoRollSnap());

    resized();
}

void MainComponent::captureSettings (Settings& settings) const
{
    settings.setPanelWidth (panelWidth);
    settings.setTabIndex (tabs.getCurrentTabIndex());
    settings.setSelectedChannelId (editorState.getSelectedChannelId());
    settings.setSelectedMixerTrackId (editorState.getSelectedMixerTrackId());
    settings.setCurrentPatternId (editorState.getCurrentPatternId());

    double zoom = 0.0, scroll = 0.0, pitch = 0.0;
    tabs.capturePianoRollView (zoom, scroll, pitch);

    settings.setPianoRollZoom (zoom);
    settings.setPianoRollScroll (scroll);
    settings.setPianoRollPitchScroll (pitch);
    settings.setPianoRollSnap (tabs.getPianoRollSnap());
}

void MainComponent::showAudioSettings()
{
    auto* panel = new AudioSettingsPanel (audioHost, engine);

    panel->onDeviceChanged = [this]
    {
        statusBar.showMessage (audioHost.describeDevice(), StatusBar::Severity::info);
    };

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (panel);
    options.dialogTitle = "Audio Settings";
    options.dialogBackgroundColour = tokens::colour::background;
    options.componentToCentreAround = this;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    transportBar.setBounds (area.removeFromTop (46));
    statusBar.setBounds (area.removeFromBottom (StatusBar::barHeight));

    const auto width = juce::jlimit (Settings::minPanelWidth,
                                     juce::jmax (Settings::minPanelWidth, area.getWidth() - 360),
                                     panelWidth);

    instrumentPanel.setBounds (area.removeFromRight (width));
    divider.setBounds (area.removeFromRight (dividerWidth));
    tabs.setBounds (area);
}

} // namespace dew
