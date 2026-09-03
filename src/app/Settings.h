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

    /** The piano roll's snap division, as its ordinal.

        The tool is deliberately not remembered: restoring someone into Paint or
        Slice means the first click of a session writes or cuts something they
        did not ask for. A grid is a preference; a loaded weapon is not.
    */
    int getPianoRollSnap() const;
    void setPianoRollSnap (int);

    /** How tall one playlist lane is, or 0 for "never set".

        Deliberately NOT clamped here, unlike the panel width. The panel's bounds
        are this class's own invention; a lane's bounds are the size ladder's,
        and dew_app cannot see the design library - it is a leaf the UI reads,
        which is the whole point of the layering. Restating the numbers here to
        clamp them would also be exactly the duplication the ladder gate exists
        to catch. So this stores a number and the playlist decides what is
        showable, which is also the only place that knows how tall a header has
        to be to hold an M and an S.
    */
    int getPlaylistTrackHeight() const;
    void setPlaylistTrackHeight (int);

    int getPanelWidth() const;
    void setPanelWidth (int);

    /** Whether the instrument panel is folded away. Kept beside its width
        rather than derived from one: a collapsed panel has to come back to the
        width it had, so the two are separate facts.
    */
    bool getPanelCollapsed() const;
    void setPanelCollapsed (bool);

    /** Whether transitions should be instant.

        Not "slower": off. Some people find animation distracting and some find
        it nauseating, and a DAW is a tool people sit in front of for hours.
    */
    bool getReduceMotion() const;
    void setReduceMotion (bool);

    /** How much bigger the whole interface is drawn.

        A multiplier on the window rather than on the type scale. dew's layout
        is a ladder of pixel sizes that a font size has to fit inside - a
        26px control holding 13pt text - so scaling only the text is how a
        caption ends up clipped by the box it was measured for. Scaling the
        PEER scales both, and the ladder keeps meaning what it says.

        Lives here rather than in the document for the same reason panel width
        does: it is a property of this person's screen, not of the music.
    */
    double getUiScale() const;
    void setUiScale (double);

    /** How tall one piano-roll pitch row is, or 0 for "never set".

        Stored the same way the playlist's lane height is, and for the same
        reason: the clamp lives with the ladder that declares the range, and
        dew_app cannot see it.
    */
    int getPianoRollRowHeight() const;
    void setPianoRollRowHeight (int);

    /** Which rung of tokens::type's code scale the score tab draws at.

        Stored RAW, and clamped where it is read - the rungs are a design-system
        fact and dew_app cannot see dew_design. getPlaylistTrackHeight stores a
        number it cannot check for exactly the same reason.
    */
    int getScoreFontStep() const;
    void setScoreFontStep (int);

    // --- rendering -----------------------------------------------------------
    /** Where the last render was written, so the next chooser opens there
        rather than wherever the system last felt like.

        Falls back to the user's Music folder, and rejects a directory that no
        longer exists - an external drive that has since been unplugged is
        exactly the "something unrecoverable" this class refuses to restore.
    */
    juce::File getLastRenderDirectory() const;
    void setLastRenderDirectory (const juce::File&);

    /** RenderFormat as an int, so this header need not include the engine. */
    int getRenderFormat() const;
    void setRenderFormat (int);

    int getRenderSampleRate() const;
    void setRenderSampleRate (int);

    int getRenderBitDepth() const;
    void setRenderBitDepth (int);

    double getRenderTailSeconds() const;
    void setRenderTailSeconds (double);

    bool getRenderNormalize() const;
    void setRenderNormalize (bool);

    // --- audio ---------------------------------------------------------------
    std::unique_ptr<juce::XmlElement> getAudioState() const;
    void setAudioState (const juce::XmlElement*);

    // --- MIDI ----------------------------------------------------------------
    /** Which MIDI channel plays: 0 for omni, or 1..16.

        Only these two live here. WHICH devices are enabled is already carried
        by the audio device state XML - AudioDeviceManager writes a MIDIINPUT
        child per enabled device, and keeps ones that are merely unplugged - so
        storing the list again here would be a second, competing answer.
    */
    int getMidiChannelFilter() const;
    void setMidiChannelFilter (int);

    /** Semitones added to every incoming note. */
    int getMidiTranspose() const;
    void setMidiTranspose (int);

    /** Writes to disk. Called on exit; safe to call more often. */
    void flush();

    static constexpr int minPanelWidth = 220;
    static constexpr int maxPanelWidth = 640;
    static constexpr int defaultPanelWidth = 300;
    /** Channel rack, piano roll, playlist, mixer, score.

        The persisted tab is a raw index, so this and EditorTabs' addTab calls
        are one fact written twice - and a new tab has to be APPENDED, or
        reopening the application lands somebody on a different editor.
    */
    static constexpr int numTabs = 5;
    static constexpr int maxMidiTranspose = 24;

    /** The range of the interface scale. 1.0 is the size dew was drawn at; the
        top is where a 46px transport strip stops fitting on a laptop display.
    */
    static constexpr double minUiScale = 1.0;
    static constexpr double maxUiScale = 2.0;
    static constexpr double defaultUiScale = 1.0;

    /** The scales the View menu offers. A short list of round numbers rather
        than a continuous slider: this is a thing you set once and forget, and
        every value in between is one more way to end up on a half-pixel grid.
    */
    static constexpr double uiScaleSteps[] = { 1.0, 1.25, 1.5, 1.75 };
    static constexpr int numUiScaleSteps = (int) (sizeof (uiScaleSteps) / sizeof (uiScaleSteps[0]));

private:
    juce::PropertiesFile& file() const { return *properties; }

    std::unique_ptr<juce::PropertiesFile> properties;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Settings)
};

} // namespace dew
