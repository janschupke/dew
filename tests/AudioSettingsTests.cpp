#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "io/LiveAudioHost.h"
#include "model/ProjectFactory.h"
#include "ui/AudioSettingsPanel.h"

using namespace dew;

TEST_CASE ("the audio panel is honest when there is no device", "[audio][ui]")
{
    // The CI case, and the one where a stock selector would show an empty
    // dropdown and no explanation.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    AudioEngine engine;
    LiveAudioHost host { engine };      // deliberately not started

    AudioSettingsPanel panel { host, engine };
    panel.setSize (AudioSettingsPanel::preferredWidth, AudioSettingsPanel::preferredHeight);
    panel.setVisible (true);
    panel.resized();

    INFO ("summary: " << panel.getSummaryText());
    REQUIRE (panel.getSummaryText().isNotEmpty());

    // It paints rather than throwing or drawing nothing at all.
    juce::Image image (juce::Image::ARGB, panel.getWidth(), panel.getHeight(), true);
    juce::Graphics g (image);
    panel.paintEntireComponent (g, true);

    int lit = 0;

    for (int y = 0; y < image.getHeight(); y += 2)
        for (int x = 0; x < image.getWidth(); x += 2)
            if (image.getPixelAt (x, y).getBrightness() > 0.3f)
                ++lit;

    REQUIRE (lit > 50);
}

TEST_CASE ("the audio panel reports the running device", "[audio][ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    AudioEngine engine;
    LiveAudioHost host { engine };

    const auto error = host.start();

    if (error.isNotEmpty() || host.getDeviceManager().getCurrentAudioDevice() == nullptr)
    {
        WARN ("no audio device on this machine; skipping the open-device case");
        return;
    }

    AudioSettingsPanel panel { host, engine };
    panel.setSize (AudioSettingsPanel::preferredWidth, AudioSettingsPanel::preferredHeight);
    panel.resized();

    INFO ("summary: " << panel.getSummaryText());

    // Sample rate, buffer size, and the latency those add up to - which is the
    // number a buffer size is actually chosen for.
    REQUIRE (panel.getSummaryText().contains ("Hz"));
    REQUIRE (panel.getSummaryText().contains ("samples"));
    REQUIRE (panel.getSummaryText().contains ("ms"));
    REQUIRE (panel.getNumDeviceOptions() > 0);

    host.stop();
}

TEST_CASE ("the test tone goes through the engine's own preview path", "[audio][ui]")
{
    // Rather than a tone generator sitting beside the engine, which would test
    // nothing about whether the engine can be heard.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    AudioEngine engine;
    engine.prepare (44100.0, 256);
    engine.setProject (ProjectFactory::createDemo());

    REQUIRE (engine.previewNoteOn (0, 69, 0.6f));

    juce::AudioBuffer<float> buffer (2, 256);
    buffer.clear();
    engine.processBlock (buffer);

    INFO ("magnitude " << buffer.getMagnitude (0, 0, buffer.getNumSamples()));
    REQUIRE (buffer.getMagnitude (0, 0, buffer.getNumSamples()) > 0.0f);
}
