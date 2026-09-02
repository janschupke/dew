#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace dew
{

/** What dew remembers between launches.

    Nothing was remembered at all: every launch opened centred at 1180x760 on
    the Channel Rack with the default audio device, whatever you were doing last
    time.

    Deliberately NOT the open project. Reopening a file means launching dew can
    confront you with state you do not remember leaving, and a document that
    looks unsaved because it was restored rather than opened. New still starts
    empty.

    Every value is validated on read. A window rectangle from a monitor that is
    no longer attached, or a channel id from a project that has since changed,
    falls back to the default rather than restoring something unrecoverable.
*/
class Settings
{
public:
    /** Uses the standard per-user location. */
    Settings();

    /** For tests: keeps the file in `directory` instead. */
    explicit Settings (const juce::File& directory);

    ~Settings();

    // --- window --------------------------------------------------------------
    /** JUCE's window state string: position, size and maximised flag. Empty if
        nothing usable was stored.
    */
    juce::String getWindowState() const;
    void setWindowState (const juce::String&);

    /** True if this rectangle is at least partly on a display that exists.
        A window restored entirely offscreen cannot be moved back by dragging it.
    */
    static bool isWindowStateUsable (const juce::String&);

    // --- view ----------------------------------------------------------------
    int getTabIndex() const;
    void setTabIndex (int);

    int getSelectedChannelId() const;
    void setSelectedChannelId (int);

    int getSelectedMixerTrackId() const;
    void setSelectedMixerTrackId (int);

    int getCurrentPatternId() const;
    void setCurrentPatternId (int);

    double getPianoRollZoom() const;
    void setPianoRollZoom (double);

    double getPianoRollScroll() const;
    void setPianoRollScroll (double);

    double getPianoRollPitchScroll() const;
    void setPianoRollPitchScroll (double);

    int getPanelWidth() const;
    void setPanelWidth (int);

    // --- audio ---------------------------------------------------------------
    std::unique_ptr<juce::XmlElement> getAudioState() const;
    void setAudioState (const juce::XmlElement*);

    /** Writes to disk. Called on exit; safe to call more often. */
    void flush();

    static constexpr int minPanelWidth = 220;
    static constexpr int maxPanelWidth = 640;
    static constexpr int defaultPanelWidth = 300;
    static constexpr int numTabs = 4;

private:
    juce::PropertiesFile& file() const { return *properties; }

    std::unique_ptr<juce::PropertiesFile> properties;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Settings)
};

} // namespace dew
