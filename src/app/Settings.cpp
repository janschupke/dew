#include "app/Settings.h"

#include "i18n/Strings.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/AssetPaths.h"
#include "model/NoteTools.h"

namespace dew
{

namespace
{

juce::PropertiesFile::Options baseOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "dew";
    options.filenameSuffix = "settings";
    options.osxLibrarySubFolder = "Application Support";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    return options;
}

} // namespace

Settings::Settings()
    : Settings (baseOptions().getDefaultFile().getParentDirectory())
{
}

Settings::Settings (const juce::File& directory)
{
    properties = std::make_unique<juce::PropertiesFile> (directory.getChildFile ("dew.settings"),
                                                         baseOptions());
}

Settings::~Settings()
{
    flush();
}

void Settings::flush()
{
    if (properties != nullptr)
        properties->saveIfNeeded();
}

// --- window ------------------------------------------------------------------

bool Settings::isWindowStateUsable (const juce::String& state)
{
    if (state.isEmpty())
        return false;

    // JUCE's format is "x y w h" with an optional leading "fs "/"fm " flag.
    auto tokens = juce::StringArray::fromTokens (state, false);

    while (! tokens.isEmpty() && ! tokens[0].containsOnly ("-0123456789"))
        tokens.remove (0);

    if (tokens.size() < 4)
        return false;

    const juce::Rectangle<int> bounds (tokens[0].getIntValue(), tokens[1].getIntValue(),
                                       tokens[2].getIntValue(), tokens[3].getIntValue());

    if (bounds.getWidth() < 200 || bounds.getHeight() < 150)
        return false;

    // Somewhere on a display that exists. A window restored entirely offscreen
    // after a monitor changes cannot be dragged back.
    for (const auto& display : juce::Desktop::getInstance().getDisplays().displays)
        if (display.userBounds.intersects (bounds.toFloat()))
            return true;

    return false;
}

juce::String Settings::getWindowState() const
{
    const auto state = file().getValue ("window");
    return isWindowStateUsable (state) ? state : juce::String();
}

void Settings::setWindowState (const juce::String& state)
{
    file().setValue ("window", state);
}

// --- view --------------------------------------------------------------------

int Settings::getTabIndex() const
{
    return juce::jlimit (0, numTabs - 1, file().getIntValue ("tab", 0));
}

void Settings::setTabIndex (int index)
{
    file().setValue ("tab", juce::jlimit (0, numTabs - 1, index));
}

int Settings::getSelectedChannelId() const
{
    return juce::jmax (1, file().getIntValue ("selectedChannel", 1));
}
void Settings::setSelectedChannelId (int id)
{
    file().setValue ("selectedChannel", juce::jmax (1, id));
}

int Settings::getSelectedMixerTrackId() const
{
    return juce::jmax (0, file().getIntValue ("selectedMixerTrack", 1));
}
void Settings::setSelectedMixerTrackId (int id)
{
    file().setValue ("selectedMixerTrack", juce::jmax (0, id));
}

int Settings::getCurrentPatternId() const
{
    return juce::jmax (1, file().getIntValue ("currentPattern", 1));
}
void Settings::setCurrentPatternId (int id)
{
    file().setValue ("currentPattern", juce::jmax (1, id));
}

// --- rendering ---------------------------------------------------------------

juce::File Settings::getLastRenderDirectory() const
{
    const juce::File stored (file().getValue ("lastRenderDir", {}));

    // A path that has gone away - a deleted folder, an unplugged drive - is not
    // restored. Opening a chooser somewhere that does not exist is worse than
    // opening it somewhere ordinary.
    if (stored.isDirectory())
        return stored;

    return AssetPaths::defaultBrowseFolder();
}

void Settings::setLastRenderDirectory (const juce::File& directory)
{
    if (directory.isDirectory())
        file().setValue ("lastRenderDir", directory.getFullPathName());
}

// --- assets ------------------------------------------------------------------

juce::File Settings::getLastSoundFontDirectory() const
{
    const juce::File stored (file().getValue ("lastSoundFontDir", {}));

    // An INVALID file rather than the browse folder, which is the one place
    // this differs from getLastRenderDirectory: "never chosen one" and "chose
    // one that has since gone" are the same answer here, and the caller has a
    // better fallback than the system's music folder - the project's own.
    return stored.isDirectory() ? stored : juce::File();
}

void Settings::setLastSoundFontDirectory (const juce::File& directory)
{
    if (directory.isDirectory())
        file().setValue ("lastSoundFontDir", directory.getFullPathName());
}

int Settings::getRenderFormat() const
{
    return juce::jlimit (0, 3, file().getIntValue ("renderFormat", 0));
}
void Settings::setRenderFormat (int f)
{
    file().setValue ("renderFormat", juce::jlimit (0, 3, f));
}

int Settings::getRenderSampleRate() const
{
    const auto stored = file().getIntValue ("renderRate", 44100);

    return (stored == 44100 || stored == 48000 || stored == 88200 || stored == 96000
            || stored == 32000)
               ? stored
               : 44100;
}

void Settings::setRenderSampleRate (int rate)
{
    file().setValue ("renderRate", rate);
}

int Settings::getRenderBitDepth() const
{
    const auto stored = file().getIntValue ("renderDepth", 24);

    return (stored == 16 || stored == 24 || stored == 32) ? stored : 24;
}

void Settings::setRenderBitDepth (int depth)
{
    file().setValue ("renderDepth", depth);
}

double Settings::getRenderTailSeconds() const
{
    return juce::jlimit (0.0, 30.0, file().getDoubleValue ("renderTail", 1.0));
}

void Settings::setRenderTailSeconds (double seconds)
{
    file().setValue ("renderTail", juce::jlimit (0.0, 30.0, seconds));
}

bool Settings::getRenderNormalize() const
{
    return file().getBoolValue ("renderNormalize", false);
}
void Settings::setRenderNormalize (bool on)
{
    file().setValue ("renderNormalize", on);
}

double Settings::getPianoRollZoom() const
{
    // Clamped to the same limits TimelineView enforces, so a corrupt value
    // cannot produce a view that is impossible to recover from.
    return juce::jlimit (3.0, 120.0, file().getDoubleValue ("pianoRollZoom", 24.0));
}

void Settings::setPianoRollZoom (double zoom)
{
    file().setValue ("pianoRollZoom", zoom);
}

double Settings::getPianoRollScroll() const
{
    return juce::jmax (0.0, file().getDoubleValue ("pianoRollScroll", 0.0));
}
void Settings::setPianoRollScroll (double s)
{
    file().setValue ("pianoRollScroll", juce::jmax (0.0, s));
}

double Settings::getPianoRollPitchScroll() const
{
    return juce::jmax (0.0, file().getDoubleValue ("pianoRollPitch", 0.0));
}
void Settings::setPianoRollPitchScroll (double s)
{
    file().setValue ("pianoRollPitch", juce::jmax (0.0, s));
}

int Settings::getPlaylistTrackHeight() const
{
    // 0 means "never set", which the playlist reads as "keep the default". A
    // negative or absurd value is stored as it was and clamped there too - the
    // clamp lives with the ladder, not here.
    return juce::jmax (0, file().getIntValue ("playlistTrackHeight", 0));
}

void Settings::setPlaylistTrackHeight (int height)
{
    file().setValue ("playlistTrackHeight", height);
}

int Settings::getMixerEffectBandHeight() const
{
    // 0 means "never set", the same as a lane's height above, and clamped in
    // the same place - the mixer, which can see the range the design system
    // declares where this cannot.
    //
    // A new key rather than the old "mixerEffectBandRows", which held a number
    // between one and four: reading a rows value as a pixel count would open
    // the band at four pixels for anyone upgrading, and the two cannot be told
    // apart by their value alone. The stale key is left in the file and
    // ignored, which costs a line of settings and no explanation.
    return juce::jmax (0, file().getIntValue ("mixerEffectBandHeight", 0));
}

void Settings::setMixerEffectBandHeight (int pixels)
{
    file().setValue ("mixerEffectBandHeight", pixels);
}

int Settings::getPianoRollSnap() const
{
    // A NEW key, deliberately. The ladder gained entries below the sixteenth -
    // "off" among them - so index 0 stopped meaning what it meant, and reading
    // the old key would silently turn the grid off for everybody who had ever
    // touched it.
    // The finest division a DEFAULT project can actually express. A sixteenth
    // at four steps to a beat is a step, so it snaps to nothing - which is what
    // the old default was, and what made Quantize look broken out of the box.
    const auto fallback = NoteTools::indexOfSnap (SnapDivision::eighth);
    const auto stored = file().getIntValue ("pianoRollSnapDivision", fallback);

    return (stored >= 0 && stored < NoteTools::numSnapDivisions) ? stored : fallback;
}

void Settings::setPianoRollSnap (int index)
{
    file().setValue ("pianoRollSnapDivision",
                     juce::jlimit (0, NoteTools::numSnapDivisions - 1, index));
}

int Settings::getPanelWidth() const
{
    return juce::jlimit (minPanelWidth, maxPanelWidth,
                         file().getIntValue ("panelWidth", defaultPanelWidth));
}

void Settings::setPanelWidth (int width)
{
    file().setValue ("panelWidth", juce::jlimit (minPanelWidth, maxPanelWidth, width));
}

bool Settings::getPanelCollapsed() const
{
    return file().getBoolValue ("panelCollapsed", false);
}

bool Settings::getMcpEnabled() const
{
    return file().getBoolValue ("mcpEnabled", false);
}

void Settings::setMcpEnabled (bool enabled)
{
    file().setValue ("mcpEnabled", enabled);
}

int Settings::getMcpPort() const
{
    // Validated on the way out, like everything else here. A port outside the
    // range a port can be - or a privileged one this could not bind anyway -
    // reads as "use the default" rather than as a failure to start.
    const auto stored = file().getIntValue ("mcpPort", 0);

    return (stored >= 1024 && stored <= 65535) ? stored : 0;
}

void Settings::setMcpPort (int port)
{
    file().setValue ("mcpPort", port);
}

juce::String Settings::getMcpGrants() const
{
    return file().getValue ("mcpGrants");
}

void Settings::setMcpGrants (const juce::String& grants)
{
    file().setValue ("mcpGrants", grants);
}

juce::String Settings::getThemeName() const
{
    return file().getValue ("theme", "dark");
}

void Settings::setThemeName (const juce::String& name)
{
    file().setValue ("theme", name);
}

juce::String Settings::getLanguage() const
{
    // Validated on the way out, the way every other stored value is: a tag this
    // build does not ship - a language removed, or a file somebody edited - is
    // an empty answer rather than a locale nothing can resolve.
    const auto stored = file().getValue ("language", "");

    if (stored.isEmpty() || availableLocales().contains (stored))
        return stored;

    return {};
}

void Settings::setLanguage (const juce::String& bcp47Tag)
{
    file().setValue ("language", bcp47Tag);
}

Settings::Motion Settings::getMotionPreference() const
{
    const auto stored = file().getValue ("motion", "system");

    if (stored == "full")
        return Motion::full;

    if (stored == "reduced")
        return Motion::reduced;

    return Motion::system;
}

void Settings::setMotionPreference (Motion motion)
{
    file().setValue ("motion", motion == Motion::full      ? "full"
                               : motion == Motion::reduced ? "reduced"
                                                           : "system");
}

bool Settings::getReduceMotion (bool systemPrefersReduced) const
{
    switch (getMotionPreference())
    {
        case Motion::full: return false;
        case Motion::reduced: return true;
        case Motion::system: break;
    }

    return systemPrefersReduced;
}

double Settings::getUiScale() const
{
    return juce::jlimit (minUiScale, maxUiScale, file().getDoubleValue ("uiScale", defaultUiScale));
}

void Settings::setUiScale (double scale)
{
    file().setValue ("uiScale", juce::jlimit (minUiScale, maxUiScale, scale));
}

int Settings::getPianoRollRowHeight() const
{
    return juce::jmax (0, file().getIntValue ("pianoRollRowHeight", 0));
}

void Settings::setPianoRollRowHeight (int height)
{
    file().setValue ("pianoRollRowHeight", height);
}

int Settings::getPianoRollVelocityHeight() const
{
    return juce::jmax (0, file().getIntValue ("pianoRollVelocityHeight", 0));
}

void Settings::setPianoRollVelocityHeight (int height)
{
    file().setValue ("pianoRollVelocityHeight", height);
}

int Settings::getScoreFontStep() const
{
    // Raw, and clamped by the reader: which rungs exist is a design-system fact
    // this layer cannot see. Negative is the one thing worth refusing here,
    // because it is never an index however many rungs there turn out to be.
    return juce::jmax (0, file().getIntValue ("scoreFontStep", 0));
}

void Settings::setScoreFontStep (int step)
{
    file().setValue ("scoreFontStep", juce::jmax (0, step));
}

void Settings::setPanelCollapsed (bool collapsed)
{
    file().setValue ("panelCollapsed", collapsed);
}

// --- audio -------------------------------------------------------------------

std::unique_ptr<juce::XmlElement> Settings::getAudioState() const
{
    return file().getXmlValue ("audioDevice");
}

void Settings::setAudioState (const juce::XmlElement* state)
{
    if (state != nullptr)
        file().setValue ("audioDevice", state);
    else
        file().removeValue ("audioDevice");
}

int Settings::getMidiChannelFilter() const
{
    return juce::jlimit (0, 16, file().getIntValue ("midiChannelFilter", 0));
}

void Settings::setMidiChannelFilter (int channel)
{
    file().setValue ("midiChannelFilter", juce::jlimit (0, 16, channel));
}

int Settings::getMidiTranspose() const
{
    return juce::jlimit (-maxMidiTranspose, maxMidiTranspose,
                         file().getIntValue ("midiTranspose", 0));
}

void Settings::setMidiTranspose (int semitones)
{
    file().setValue ("midiTranspose",
                     juce::jlimit (-maxMidiTranspose, maxMidiTranspose, semitones));
}

bool Settings::getMetronomeEnabled() const
{
    return file().getBoolValue ("metronomeEnabled", false);
}

void Settings::setMetronomeEnabled (bool shouldClick)
{
    file().setValue ("metronomeEnabled", shouldClick);
}

bool Settings::getCountInEnabled() const
{
    return file().getBoolValue ("countInEnabled", false);
}

void Settings::setCountInEnabled (bool shouldCountIn)
{
    file().setValue ("countInEnabled", shouldCountIn);
}

bool Settings::getKeyboardInputEnabled() const
{
    return file().getBoolValue ("keyboardInputEnabled", false);
}

void Settings::setKeyboardInputEnabled (bool shouldPlay)
{
    file().setValue ("keyboardInputEnabled", shouldPlay);
}

int Settings::getKeyboardInputOctave() const
{
    return juce::jlimit (minKeyboardOctave, maxKeyboardOctave,
                         file().getIntValue ("keyboardInputOctave", defaultKeyboardOctave));
}

void Settings::setKeyboardInputOctave (int octave)
{
    file().setValue ("keyboardInputOctave",
                     juce::jlimit (minKeyboardOctave, maxKeyboardOctave, octave));
}

} // namespace dew
