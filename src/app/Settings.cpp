#include "Settings.h"

#include <juce_gui_basics/juce_gui_basics.h>

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

void Settings::setTabIndex (int index) { file().setValue ("tab", juce::jlimit (0, numTabs - 1, index)); }

int Settings::getSelectedChannelId() const   { return juce::jmax (1, file().getIntValue ("selectedChannel", 1)); }
void Settings::setSelectedChannelId (int id) { file().setValue ("selectedChannel", juce::jmax (1, id)); }

int Settings::getSelectedMixerTrackId() const   { return juce::jmax (0, file().getIntValue ("selectedMixerTrack", 1)); }
void Settings::setSelectedMixerTrackId (int id) { file().setValue ("selectedMixerTrack", juce::jmax (0, id)); }

int Settings::getCurrentPatternId() const   { return juce::jmax (1, file().getIntValue ("currentPattern", 1)); }
void Settings::setCurrentPatternId (int id) { file().setValue ("currentPattern", juce::jmax (1, id)); }

// --- rendering ---------------------------------------------------------------

juce::File Settings::getLastRenderDirectory() const
{
    const juce::File stored (file().getValue ("lastRenderDir", {}));

    // A path that has gone away - a deleted folder, an unplugged drive - is not
    // restored. Opening a chooser somewhere that does not exist is worse than
    // opening it somewhere ordinary.
    if (stored.isDirectory())
        return stored;

    return juce::File::getSpecialLocation (juce::File::userMusicDirectory);
}

void Settings::setLastRenderDirectory (const juce::File& directory)
{
    if (directory.isDirectory())
        file().setValue ("lastRenderDir", directory.getFullPathName());
}

int Settings::getRenderFormat() const   { return juce::jlimit (0, 3, file().getIntValue ("renderFormat", 0)); }
void Settings::setRenderFormat (int f)  { file().setValue ("renderFormat", juce::jlimit (0, 3, f)); }

int Settings::getRenderSampleRate() const
{
    const auto stored = file().getIntValue ("renderRate", 44100);

    return (stored == 44100 || stored == 48000 || stored == 88200 || stored == 96000
            || stored == 32000)
             ? stored : 44100;
}

void Settings::setRenderSampleRate (int rate) { file().setValue ("renderRate", rate); }

int Settings::getRenderBitDepth() const
{
    const auto stored = file().getIntValue ("renderDepth", 24);

    return (stored == 16 || stored == 24 || stored == 32) ? stored : 24;
}

void Settings::setRenderBitDepth (int depth) { file().setValue ("renderDepth", depth); }

double Settings::getRenderTailSeconds() const
{
    return juce::jlimit (0.0, 30.0, file().getDoubleValue ("renderTail", 1.0));
}

void Settings::setRenderTailSeconds (double seconds)
{
    file().setValue ("renderTail", juce::jlimit (0.0, 30.0, seconds));
}

bool Settings::getRenderNormalize() const    { return file().getBoolValue ("renderNormalize", false); }
void Settings::setRenderNormalize (bool on)  { file().setValue ("renderNormalize", on); }

double Settings::getPianoRollZoom() const
{
    // Clamped to the same limits TimelineView enforces, so a corrupt value
    // cannot produce a view that is impossible to recover from.
    return juce::jlimit (3.0, 120.0, file().getDoubleValue ("pianoRollZoom", 24.0));
}

void Settings::setPianoRollZoom (double zoom) { file().setValue ("pianoRollZoom", zoom); }

double Settings::getPianoRollScroll() const   { return juce::jmax (0.0, file().getDoubleValue ("pianoRollScroll", 0.0)); }
void Settings::setPianoRollScroll (double s)  { file().setValue ("pianoRollScroll", juce::jmax (0.0, s)); }

double Settings::getPianoRollPitchScroll() const  { return juce::jmax (0.0, file().getDoubleValue ("pianoRollPitch", 0.0)); }
void Settings::setPianoRollPitchScroll (double s) { file().setValue ("pianoRollPitch", juce::jmax (0.0, s)); }

int Settings::getPianoRollSnap() const
{
    // A division that does not exist falls back to the finest one, which is the
    // behaviour the roll had before there was a grid at all.
    const auto stored = file().getIntValue ("pianoRollSnap", 0);

    return (stored >= 0 && stored < NoteTools::numSnapDivisions) ? stored : 0;
}

void Settings::setPianoRollSnap (int index)
{
    file().setValue ("pianoRollSnap", juce::jlimit (0, NoteTools::numSnapDivisions - 1, index));
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
    file().setValue ("midiTranspose", juce::jlimit (-maxMidiTranspose, maxMidiTranspose, semitones));
}

} // namespace dew
