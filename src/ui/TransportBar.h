#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "model/Meter.h"
#include "app/ProjectDocument.h"
#include "ui/ConfirmPanel.h"
#include "ui/EditorState.h"
#include "ui/design/SignalScope.h"
#include "ui/primitives/DewControls.h"
#include "ui/ParamContextMenu.h"
#include "ui/primitives/DewNumberField.h"

namespace dew
{

/** Play/stop, tempo, pattern-or-song, the current pattern, and the playhead. */
class TransportBar : public juce::Component,
                     private juce::Timer,
                     private juce::ChangeListener,
                     private juce::ValueTree::Listener
{
public:
    /** The tempo field's right-click. It is the one control outside the editors
        that names a parameter, and now that tempo is automatable it offers a
        curve like any other. */
    void setParamMenuHost (const paramMenu::Host*);

    TransportBar (ProjectDocument&, AudioEngine&, EditorState&);
    ~TransportBar() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Called when the document is replaced wholesale. */
    void refresh();

    /** Points the editor, the engine and the dropdown at `wantedId`, or at the
        project's FIRST pattern when it has no such pattern.

        The rule lived inside rebuildPatternList, which meant it ran on every
        path that replaced the document and on none of the paths that only
        restored a remembered id. Startup was the second kind: applySettings
        wrote a pattern id straight into EditorState, and a stale one - a
        project that has since lost that pattern, or a settings file written
        beside a different one - left the dropdown blank, the length field
        disabled, the roll empty and the sequencer with nothing to play.

        Answered from the DOCUMENT rather than from the dropdown. The dropdown's
        first row is a pattern only while there is one; with an empty project it
        is the "New pattern" sentinel, and that used to reach the engine as a
        pattern id of a million.
    */
    void setCurrentPattern (int wantedId);

    /** Starts or stops a take. Wired by the parent rather than reached through
        a back-pointer, like every other cross-component call in the editor -
        recording needs the document, the device and the playlist at once, and
        none of those belong to a transport bar.
    */
    std::function<void()> onToggleRecord;

    /** What deleting a pattern asks first. See ConfirmHook. */
    ConfirmHook confirmDestructive;

    /** Whether a take is running, for the button's lit state. */
    std::function<bool()> isRecording;

    /** Fired after the metre changed and the arrangement was rescaled with it.
        The flag is false when a clip had to round to a whole bar, so the host
        can say so - a rescale by 4/3 cannot land every clip exactly, and that
        is worth one line in the status bar rather than a discovery later.
    */
    std::function<void (bool wasExact)> onMeterChanged;

    /** The bars:beats:ticks readout, as a pure function of a position and a
        metre.

        Separated from the label so the counting rule can be checked without a
        component or an engine behind it - and because the engine round-trips
        the playhead through an integer sample count, so a component test of
        this would be asserting on a position a hair under the one it asked
        for.

        Bars, beats and ticks all count from one, the way a musician does.
    */
    static juce::String positionText (double steps, const Meter&);

    /** The pattern dropdown's "New pattern" row. Far above any pattern id, so
        it can never be mistaken for one - and public because anything counting
        the patterns in the box has to know to skip it.
    */
    static constexpr int newPatternItemId = 1'000'000;

private:
    /** True for every value after the first in one number-field drag, so a
        whole drag is one undo step rather than one per frame. */
    bool tempoGestureActive = false;
    bool lengthGestureActive = false;

    /** Adds a pattern and makes it current. The + button and the dropdown's own
        "New pattern" item both go through here.
    */
    void addPattern();

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;

    void rebuildPatternList();
    void updatePositionLabel();
    void refreshPatternLength();
    void rebuildMeterList();
    void refreshMeter();
    void applyMeterChoice (int itemId);
    juce::ValueTree currentPattern() const;

    /** Patterns in the DOCUMENT. The pattern box is not a count of them: it
        carries a "New pattern" row of its own. */
    int countPatterns() const;

    /** Asks, then deletes. Separate from the button so the question and the
        edit can be read in one place. */
    void requestDeletePattern();

    ProjectDocument& document;
    AudioEngine& engine;
    EditorState& editorState;

    DewIconButton playButton { icons::play(), "Play or pause (Space)", DewIconButton::Role::go };
    DewIconButton stopButton { icons::stop(), "Stop and rewind" };
    DewIconButton recordButton { icons::record(), "Record into the armed channel (R)",
                                 DewIconButton::Role::record };
    const paramMenu::Host* paramMenuHost = nullptr;

    DewNumberField tempoField;

    /** The meter, beside the tempo because it is the other thing that governs
        the whole project's time. A combo rather than two number fields: the
        denominator is a note value, so only five values are legal, and the pair
        reads as one thing - "4/4" - rather than as two numbers to reconcile.
    */
    DewDropdown meterBox;

    DewButton modeButton { "Pattern", DewButton::Role::normal };

    // Pattern management lives here because the pattern selector does: adding a
    // pattern used to be reachable only from a menu shortcut, which meant it
    // read as "you cannot add more patterns".
    DewLabel patternCaption;
    DewDropdown patternBox;
    DewIconButton addPatternButton { icons::plus(), "Add a pattern" };
    DewIconButton clonePatternButton { icons::duplicate(), "Duplicate this pattern" };
    DewIconButton deletePatternButton { icons::trash(), "Delete this pattern",
                                        DewIconButton::Role::danger };
    DewNumberField patternLengthField;

    DewLabel positionLabel;

    /** The right-hand end of the bar, and the only thing in it that gives way
        when the window narrows.
    */
    SignalScope signalScope;

    bool updatingPatternList = false;
    bool updatingMeterBox = false;

    /** The meters offered, in the order they are listed. Beats per bar and the
        beat unit; anything else is reachable only by editing the file, and is
        clamped and shown as the nearest entry.
    */
    struct MeterChoice
    {
        int beatsPerBar, beatUnit;
    };
    static const MeterChoice meterChoices[];
    static const int numMeterChoices;
    juce::Array<int> groupDividers;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBar)
};

} // namespace dew
