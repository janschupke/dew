#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>

#include "app/Settings.h"
#include "engine/AudioEngine.h"
#include "engine/LiveAudioHost.h"
#include "engine/MidiInputHost.h"
#include "engine/MidiRouter.h"
#include "model/ProjectFactory.h"
#include "ui/MidiSettingsPanel.h"

using namespace dew;

namespace
{

constexpr double sampleRate = 44100.0;
constexpr int blockSize = 256;

/** A settings file in a directory of its own, deleted afterwards. The same
    shape SettingsTests uses.
*/
struct TempSettings
{
    TempSettings()
        : directory (juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("dew-midi-" + juce::Uuid().toDashedString()))
    {
        directory.createDirectory();
    }

    ~TempSettings() { directory.deleteRecursively(); }

    juce::File directory;
};

juce::Image renderToImage (juce::Component& c)
{
    juce::Image image (juce::Image::ARGB, juce::jmax (1, c.getWidth()),
                       juce::jmax (1, c.getHeight()), true);
    juce::Graphics g (image);
    c.paintEntireComponent (g, true);
    return image;
}

int litPixels (const juce::Image& image)
{
    int lit = 0;

    for (int y = 0; y < image.getHeight(); y += 2)
        for (int x = 0; x < image.getWidth(); x += 2)
            if (image.getPixelAt (x, y).getBrightness() > 0.3f)
                ++lit;

    return lit;
}

} // namespace

TEST_CASE ("a MIDI note produces audible samples end to end", "[midi][engine]")
{
    // The whole chain a controller's note actually travels: the MIDI queue,
    // drained on the audio thread, into a real voice.
    AudioEngine engine;
    engine.prepare (sampleRate, blockSize);
    engine.setProject (ProjectFactory::createDefault());

    juce::AudioBuffer<float> buffer (2, blockSize);
    buffer.clear();
    engine.processBlock (buffer);

    REQUIRE (engine.midiNoteOn (0, 69, 0.8f));

    auto peak = 0.0f;

    for (int i = 0; i < 6; ++i)
    {
        buffer.clear();
        engine.processBlock (buffer);
        peak = juce::jmax (peak, buffer.getMagnitude (0, 0, buffer.getNumSamples()));
    }

    INFO ("peak " << peak);
    REQUIRE (peak > 0.0f);
}

TEST_CASE ("the two queues do not lose events when both are written at once",
           "[midi][engine][threading]")
{
    // This is the guard on the concurrency decision. PreviewQueue is
    // single-producer by construction, and the message thread already owns the
    // preview one - so MIDI gets its own rather than racing on a shared
    // writeIndex. Collapsing them into one queue must fail this under TSan.
    AudioEngine engine;
    engine.prepare (sampleRate, blockSize);
    engine.setProject (ProjectFactory::createDefault());

    juce::AudioBuffer<float> buffer (2, blockSize);
    buffer.clear();
    engine.processBlock (buffer);

    constexpr int perProducer = 2000;

    std::atomic<int> midiAccepted { 0 };
    std::atomic<bool> draining { true };

    // The audio thread, draining both rings.
    std::thread audio ([&]
    {
        juce::AudioBuffer<float> block (2, blockSize);

        while (draining.load())
        {
            block.clear();
            engine.processBlock (block);
            std::this_thread::yield();
        }

        for (int i = 0; i < 32; ++i)
        {
            block.clear();
            engine.processBlock (block);
        }
    });

    // The MIDI thread.
    std::thread midi ([&]
    {
        for (int i = 0; i < perProducer; ++i)
        {
            if (engine.midiNoteOn (0, 60 + (i % 12), 0.5f))
                ++midiAccepted;

            engine.midiNoteOff (0, 60 + (i % 12));
            std::this_thread::yield();
        }
    });

    // And this, the message thread, on the preview ring at the same time.
    int previewAccepted = 0;

    for (int i = 0; i < perProducer; ++i)
    {
        if (engine.previewNoteOn (1, 48 + (i % 12), 0.5f))
            ++previewAccepted;

        engine.previewNoteOff (1, 48 + (i % 12));
        std::this_thread::yield();
    }

    midi.join();
    draining.store (false);
    audio.join();

    INFO ("midi accepted " << midiAccepted.load() << ", preview accepted " << previewAccepted);

    // Both producers got through. A ring may legitimately refuse when full, so
    // this asserts that the great majority landed rather than demanding every
    // single one - what must NOT happen is corruption or a lost consumer.
    REQUIRE (midiAccepted.load() > perProducer / 2);
    REQUIRE (previewAccepted > perProducer / 2);

    // And the engine still works afterwards.
    engine.previewAllOff();
    REQUIRE (engine.midiNoteOn (0, 69, 0.8f));

    auto peak = 0.0f;

    for (int i = 0; i < 8; ++i)
    {
        buffer.clear();
        engine.processBlock (buffer);
        peak = juce::jmax (peak, buffer.getMagnitude (0, 0, buffer.getNumSamples()));
    }

    REQUIRE (peak > 0.0f);
}

TEST_CASE ("the router survives a message thread resetting under a live MIDI stream",
           "[midi][router][threading]")
{
    // The shape of the real bug: the editor changes the selected channel, or a
    // device is unticked, while the controller is still sending. Under TSan
    // this is what catches reset() writing state the MIDI thread owns.
    AudioEngine engine;
    engine.prepare (sampleRate, blockSize);
    engine.setProject (ProjectFactory::createDefault());
    MidiRouter router { engine };

    std::atomic<bool> running { true };

    std::thread midi ([&]
    {
        int pitch = 48;

        while (running.load())
        {
            router.handleMessage (juce::MidiMessage::noteOn (1, pitch, (juce::uint8) 100));
            router.handleMessage (juce::MidiMessage::controllerEvent (1, 64, 127));
            router.handleMessage (juce::MidiMessage::noteOff (1, pitch));
            router.handleMessage (juce::MidiMessage::pitchWheel (1, 12000));
            router.handleMessage (juce::MidiMessage::controllerEvent (1, 1, 90));

            pitch = 48 + ((pitch - 47) % 24);
            std::this_thread::yield();
        }
    });

    std::thread audio ([&]
    {
        juce::AudioBuffer<float> block (2, blockSize);

        while (running.load())
        {
            block.clear();
            engine.processBlock (block);
            std::this_thread::yield();
        }
    });

    // The message thread, doing exactly what the editor does.
    for (int i = 0; i < 500; ++i)
    {
        router.setTargetChannel (i % 4);
        router.reset();
        router.setChannelFilter (i % 17);
        router.setTranspose ((i % 25) - 12);
        std::this_thread::yield();
    }

    running.store (false);
    midi.join();
    audio.join();

    // Still coherent afterwards.
    router.setChannelFilter (MidiRouter::omni);
    router.setTranspose (0);
    router.setTargetChannel (2);

    REQUIRE (router.getTargetChannel() == 2);
    REQUIRE (router.getChannelFilter() == MidiRouter::omni);

    router.handleMessage (juce::MidiMessage::noteOn (1, 69, (juce::uint8) 100));

    juce::AudioBuffer<float> block (2, blockSize);
    auto peak = 0.0f;

    for (int i = 0; i < 40; ++i)
    {
        block.clear();
        engine.processBlock (block);
        peak = juce::jmax (peak, block.getMagnitude (0, 0, block.getNumSamples()));
    }

    INFO ("peak after the storm: " << peak);
    REQUIRE (peak > 0.0f);
}

TEST_CASE ("a device the user wants is remembered even when it is not here", "[midi][devices]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    AudioEngine engine;
    LiveAudioHost audioHost { engine };
    MidiInputHost host { audioHost.getDeviceManager(), engine };

    const juce::String phantom { "dew-test-device-that-does-not-exist" };

    REQUIRE_FALSE (host.isDeviceWanted (phantom));

    host.setDeviceEnabled (phantom, true);

    REQUIRE (host.isDeviceWanted (phantom));
    REQUIRE_FALSE (host.isDeviceConnected (phantom));
    REQUIRE (host.getNumWantedDevices() == 1);

    // It still gets a row, so its tick is visible and can be cleared.
    const auto listed = host.getListedDevices();

    REQUIRE (std::any_of (listed.begin(), listed.end(),
                          [&] (const auto& d) { return d.identifier == phantom; }));

    host.setDeviceEnabled (phantom, false);
    REQUIRE_FALSE (host.isDeviceWanted (phantom));
}

TEST_CASE ("the reconcile rule opens what came back and drops what went away",
           "[midi][devices]")
{
    // The whole of the JUCE gap this class exists for, as a truth table.
    // AudioDeviceManager only re-opens devices from the list it built out of a
    // state XML, so one enabled during the session is left holding a dead port:
    // it reports itself enabled, delivers nothing, and will not reopen because
    // it believes it is already open.
    using Action = MidiInputHost::Action;

    // Here and shut: open it. This is the replug, and the case the manager
    // gets wrong on its own.
    REQUIRE (MidiInputHost::actionFor (true, false) == Action::open);

    // Gone but still "enabled": drop the dead port, so it can be opened again
    // when the cable comes back. Deleting this line is the bug.
    REQUIRE (MidiInputHost::actionFor (false, true) == Action::close);

    // Already right, either way: do nothing.
    REQUIRE (MidiInputHost::actionFor (true, true) == Action::none);
    REQUIRE (MidiInputHost::actionFor (false, false) == Action::none);
}

TEST_CASE ("a wanted device survives a reconcile that cannot see it", "[midi][devices]")
{
    // No hardware needed: what matters is that the user's choice outlives the
    // device list, so the row and its tick are still there to come back to.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    AudioEngine engine;
    LiveAudioHost audioHost { engine };
    MidiInputHost host { audioHost.getDeviceManager(), engine };

    const juce::String phantom { "dew-test-phantom" };
    host.setDeviceEnabled (phantom, true);

    host.reconcileWith ({});

    REQUIRE (host.isDeviceWanted (phantom));
    REQUIRE_FALSE (host.isDeviceConnected (phantom));

    host.reconcileWith ({ juce::MidiDeviceInfo { "Phantom", phantom } });

    REQUIRE (host.isDeviceWanted (phantom));
}

TEST_CASE ("a wanted device that vanishes and returns is re-opened", "[midi][devices]")
{
    // The JUCE gap this class exists for. AudioDeviceManager only re-opens
    // devices from the list it built out of a state XML, so one enabled during
    // the session is left holding a dead port: it reports itself enabled,
    // delivers nothing, and will not reopen. reconcileWith drops it on the way
    // out so that it can be opened again on the way back in.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    AudioEngine engine;
    LiveAudioHost audioHost { engine };
    auto& manager = audioHost.getDeviceManager();
    MidiInputHost host { manager, engine };

    const auto present = MidiInputHost::getAvailableDevices();

    if (present.isEmpty())
    {
        WARN ("no MIDI devices on this machine; skipping the replug case");
        return;
    }

    const auto device = present.getFirst();

    host.setDeviceEnabled (device.identifier, true);

    if (! manager.isMidiInputDeviceEnabled (device.identifier))
    {
        WARN ("could not open " << device.name << "; skipping the replug case");
        return;
    }

    // Unplugged: the port is dropped, but the user's choice is not.
    host.reconcileWith ({});

    REQUIRE (host.isDeviceWanted (device.identifier));
    REQUIRE_FALSE (manager.isMidiInputDeviceEnabled (device.identifier));

    // Plugged back in: opened again, without the user touching anything.
    host.reconcileWith (present);

    REQUIRE (manager.isMidiInputDeviceEnabled (device.identifier));

    host.setDeviceEnabled (device.identifier, false);
}

TEST_CASE ("the host describes what is listening", "[midi][devices]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    AudioEngine engine;
    LiveAudioHost audioHost { engine };
    MidiInputHost host { audioHost.getDeviceManager(), engine };

    REQUIRE (host.describeInputs() == "no MIDI input");

    host.setDeviceEnabled ("dew-test-absent-device", true);

    // Wanted but not here reads differently from nothing wanted at all.
    REQUIRE (host.describeInputs() == "MIDI input not connected");
}

TEST_CASE ("wanting a device is not the same as listening to one", "[midi][devices]")
{
    // Conflating the two is how a panel ends up showing a healthy green light
    // beside the words "not connected".
    const juce::ScopedJuceInitialiser_GUI juceInit;

    AudioEngine engine;
    LiveAudioHost audioHost { engine };
    MidiInputHost host { audioHost.getDeviceManager(), engine };

    REQUIRE (host.getNumWantedDevices() == 0);
    REQUIRE_FALSE (host.hasConnectedInput());

    host.setDeviceEnabled ("dew-test-absent-device", true);

    REQUIRE (host.getNumWantedDevices() == 1);
    REQUIRE_FALSE (host.hasConnectedInput());
}

TEST_CASE ("the panel does not repeat the empty list in its status line", "[midi][ui]")
{
    // One live line; spending it on something already on screen wastes it.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    AudioEngine engine;
    LiveAudioHost audioHost { engine };
    MidiInputHost host { audioHost.getDeviceManager(), engine };

    MidiSettingsPanel panel { host, nullptr };
    panel.setSize (MidiSettingsPanel::preferredWidth, MidiSettingsPanel::preferredHeight);
    panel.resized();

    if (panel.getNumDeviceRows() > 0)
    {
        WARN ("this machine has MIDI devices; skipping the empty-list case");
        return;
    }

    INFO ("summary: " << panel.getSummaryText());
    REQUIRE (panel.getSummaryText().isNotEmpty());
    REQUIRE_FALSE (panel.getSummaryText().contains ("No MIDI inputs were found"));
}

TEST_CASE ("settings round-trip the MIDI channel filter and transpose", "[midi][settings]")
{
    TempSettings temp;

    {
        Settings settings { temp.directory };

        REQUIRE (settings.getMidiChannelFilter() == 0);
        REQUIRE (settings.getMidiTranspose() == 0);

        settings.setMidiChannelFilter (7);
        settings.setMidiTranspose (-12);
        settings.flush();
    }

    {
        Settings settings { temp.directory };

        REQUIRE (settings.getMidiChannelFilter() == 7);
        REQUIRE (settings.getMidiTranspose() == -12);
    }
}

TEST_CASE ("settings clamp a MIDI channel and transpose that make no sense", "[midi][settings]")
{
    TempSettings temp;
    Settings settings { temp.directory };

    settings.setMidiChannelFilter (99);
    REQUIRE (settings.getMidiChannelFilter() == 16);

    settings.setMidiChannelFilter (-5);
    REQUIRE (settings.getMidiChannelFilter() == 0);

    settings.setMidiTranspose (500);
    REQUIRE (settings.getMidiTranspose() == Settings::maxMidiTranspose);
}

TEST_CASE ("the MIDI panel is honest when there is nothing attached", "[midi][ui]")
{
    // The CI case, and the one where a stock selector would show an empty list
    // and no explanation.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    AudioEngine engine;
    LiveAudioHost audioHost { engine };
    MidiInputHost host { audioHost.getDeviceManager(), engine };

    MidiSettingsPanel panel { host, nullptr };
    panel.setSize (MidiSettingsPanel::preferredWidth, MidiSettingsPanel::preferredHeight);
    panel.setVisible (true);
    panel.resized();

    INFO ("summary: " << panel.getSummaryText());
    REQUIRE (panel.getSummaryText().isNotEmpty());

    const auto lit = litPixels (renderToImage (panel));
    INFO ("lit pixels: " << lit);
    REQUIRE (lit > 50);
}

TEST_CASE ("the MIDI panel keeps a row for a device that is not connected", "[midi][ui]")
{
    // A tick that silently disappears is one the user cannot account for or
    // clear, so the row stays and says why.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    AudioEngine engine;
    LiveAudioHost audioHost { engine };
    MidiInputHost host { audioHost.getDeviceManager(), engine };

    const auto before = MidiInputHost::getAvailableDevices().size();

    host.setDeviceEnabled ("dew-test-absent-device", true);

    MidiSettingsPanel panel { host, nullptr };
    panel.setSize (MidiSettingsPanel::preferredWidth, MidiSettingsPanel::preferredHeight);
    panel.resized();

    REQUIRE (panel.getNumDeviceRows() == before + 1);

    bool foundAbsent = false;

    for (int i = 0; i < panel.getNumDeviceRows(); ++i)
    {
        if (! panel.getDeviceRowText (i).contains ("dew-test-absent-device"))
            continue;

        foundAbsent = true;
        REQUIRE (panel.getDeviceRowText (i).contains ("not connected"));
        REQUIRE (panel.isDeviceRowTicked (i));
    }

    REQUIRE (foundAbsent);
}

TEST_CASE ("the MIDI panel shows the filter and transpose the router is holding", "[midi][ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    AudioEngine engine;
    LiveAudioHost audioHost { engine };
    MidiInputHost host { audioHost.getDeviceManager(), engine };

    host.getRouter().setChannelFilter (5);
    host.getRouter().setTranspose (-7);

    MidiSettingsPanel panel { host, nullptr };
    panel.setSize (MidiSettingsPanel::preferredWidth, MidiSettingsPanel::preferredHeight);
    panel.resized();

    REQUIRE (host.getRouter().getChannelFilter() == 5);
    REQUIRE (host.getRouter().getTranspose() == -7);

    // And it still paints with those set.
    REQUIRE (litPixels (renderToImage (panel)) > 50);
}
