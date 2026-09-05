#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "app/Settings.h"
#include "engine/AudioEngine.h"
#include "io/LiveAudioHost.h"
#include "io/MidiInputHost.h"
#include "ui/AboutPanel.h"
#include "ui/AudioSettingsPanel.h"
#include "ui/ConfirmPanel.h"
#include "ui/McpConnectionsPanel.h"
#include "ui/McpConsentPanel.h"
#include "ui/MidiSettingsPanel.h"
#include "ui/PreferencesCatalog.h"
#include "ui/PreferencesPanel.h"
#include "ui/RandomizePanel.h"

/** The modes that shoot ONE bare panel, and the PNG writer they all share.

    Eight of dew_shot's modes were the same twelve lines with a different type
    in the middle: build the panel, show it, size it to its own preferred size,
    write it, say what was written. That repetition is why they live together
    now - and it is what kept shot_main.cpp two lines under the 400-code-line
    gate, with two more panels waiting to be added.

    A panel is shot BARE, never in a DialogWindow: dew_shot paints into an
    Image with no peer, and a DialogWindow is a window. That is the whole reason
    every dialog panel in dew declares preferredWidth and preferredHeight
    publicly.
*/
namespace dew::shot
{

/** `scale` renders AT that scale rather than drawing at 1x and resampling.

    Text on a control surface is the whole subject of these shots, and a
    resampled 11px caption is mush. The component still lays out in logical
    pixels, so a 2x shot is the same picture with more of it in rather than a
    different one.
*/
inline juce::Result writePng (juce::Component& component, const juce::File& destination, int scale)
{
    juce::Image image (juce::Image::ARGB, component.getWidth() * scale,
                       component.getHeight() * scale, true);

    {
        juce::Graphics g (image);
        g.addTransform (juce::AffineTransform::scale ((float) scale));
        component.paintEntireComponent (g, true);
    }

    destination.getParentDirectory().createDirectory();
    destination.deleteFile();

    auto stream = std::unique_ptr<juce::FileOutputStream> (destination.createOutputStream());

    if (stream == nullptr)
        return juce::Result::fail ("could not create " + destination.getFullPathName());

    juce::PNGImageFormat png;

    if (! png.writeImageToStream (image, *stream))
        return juce::Result::fail ("could not encode a PNG");

    return juce::Result::ok();
}

/** Sizes `panel` to its own preferred size, writes it, and reports.

    @returns the process exit code.
*/
template <typename Panel> int shoot (Panel& panel, const juce::File& destination, int scale)
{
    panel.setVisible (true);
    panel.setSize (Panel::preferredWidth, Panel::preferredHeight);

    if (const auto result = writePng (panel, destination, scale); result.failed())
    {
        std::cerr << "dew_shot: " << result.getErrorMessage() << std::endl;
        return 1;
    }

    std::cout << "wrote " << destination.getFullPathName() << "  (" << panel.getWidth() << "x"
              << panel.getHeight() << ")" << std::endl;
    return 0;
}

/** The preferences window's page names, as --page takes them. */
inline prefs::Page preferencesPageFor (const juce::String& name)
{
    for (const auto page : prefs::pageOrder())
        if (tr (prefs::titleOf (page)).toLowerCase() == name.toLowerCase())
            return page;

    return prefs::Page::appearance;
}

/** The one-panel modes.

    @param page    preferences only: which page to bring to the front.
    @param filter  preferences only: a query to apply, so the searching state -
                   the narrowed sidebar and the flat result list - can be looked
                   at rather than only reasoned about.

    @returns the exit code, or -1 when `mode` is not one of these - which is how
             main() knows to go on and try the editor modes.
*/
inline int shootPanel (const juce::String& mode, const juce::File& destination, int scale,
                       const juce::String& page = {}, const juce::String& filter = {})
{
    if (mode == "audio")
    {
        // Without a device open, which is both the CI case and the one worth
        // looking at: the panel has to be honest rather than blank.
        AudioEngine engine;
        LiveAudioHost host { engine };

        AudioSettingsPanel panel { host, engine };
        return shoot (panel, destination, scale);
    }

    if (mode == "midi")
    {
        // The panel with whatever this machine actually has attached, which on
        // CI is nothing - and "nothing" is the state most worth looking at.
        AudioEngine engine;
        LiveAudioHost host { engine };
        MidiInputHost midiHost { host.getDeviceManager(), engine };

        MidiSettingsPanel panel { midiHost, nullptr };
        return shoot (panel, destination, scale);
    }

    if (mode == "confirm")
    {
        ConfirmPanel panel { { "Delete pattern",
                               "Delete \"Groove\"? Every clip that plays it goes with it.",
                               "Delete" } };
        return shoot (panel, destination, scale);
    }

    if (mode == "mcp-consent")
    {
        McpConsentPanel panel { { "Claude Code", "1.2.3" } };
        return shoot (panel, destination, scale);
    }

    if (mode == "mcp")
    {
        // No server and no settings: the panel is a VIEW, and the state it
        // shows when there is neither is exactly the state worth a picture -
        // the switch off and nothing listening.
        McpConnectionsPanel panel { {}, nullptr };
        return shoot (panel, destination, scale);
    }

    if (mode == "randomize")
    {
        RandomizePanel panel { {}, "Applies to the 12 selected notes" };
        return shoot (panel, destination, scale);
    }

    if (mode == "about")
    {
        AboutPanel panel;
        return shoot (panel, destination, scale);
    }

    if (mode == "preferences")
    {
        // A Settings kept in the temp directory, not the real one: rendering a
        // picture of this window must not touch what the application remembers.
        // The devices are built the way the audio and midi modes build them, so
        // those two pages show what they would show on this machine.
        Settings settings {
            juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("dew-shot")
        };

        AudioEngine engine;
        LiveAudioHost host { engine };
        MidiInputHost midiHost { host.getDeviceManager(), engine };

        PreferencesPanel::Hosts hosts;
        hosts.audio = &host;
        hosts.engine = &engine;
        hosts.midi = &midiHost;

        // No command manager and no endpoint: the controls show the right
        // values, and there is no application for changing one to reach.
        PreferencesPanel panel { settings, std::move (hosts) };

        panel.setSize (PreferencesPanel::preferredWidth, PreferencesPanel::preferredHeight);

        if (page.isNotEmpty())
            panel.selectPage (preferencesPageFor (page));

        if (filter.isNotEmpty())
            panel.setFilter (filter);

        return shoot (panel, destination, scale);
    }

    return -1;
}

} // namespace dew::shot
