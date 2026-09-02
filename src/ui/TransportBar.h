#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../engine/AudioEngine.h"
#include "../model/Meter.h"
#include "../model/ProjectDocument.h"
#include "EditorState.h"
#include "SignalScope.h"
#include "primitives/DewControls.h"
#include "primitives/DewNumberField.h"

namespace dew
{

/** Play/stop, tempo, pattern-or-song, the current pattern, and the playhead. */
class TransportBar : public juce::Component,
                     private juce::Timer,
                     private juce::ChangeListener,
                     private juce::ValueTree::Listener
{
public:
    TransportBar (ProjectDocument&, AudioEngine&, EditorState&);
    ~TransportBar() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Called when the document is replaced wholesale. */
    void refresh();

    /** Starts or stops a take. Wired by the parent rather than reached through
        a back-pointer, like every other cross-component call in the editor -
        recording needs the document, the device and the playlist at once, and
        none of those belong to a transport bar.
    */
    std::function<void()> onToggleRecord;

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

    ProjectDocument& document;
    AudioEngine& engine;
    EditorState& editorState;

    DewIconButton playButton { icons::play(), "Play or pause (Space)" };
    DewIconButton stopButton { icons::stop(), "Stop and rewind" };
    DewIconButton recordButton { icons::record(), "Record into the armed channel (R)" };
    DewNumberField tempoField;

    /** The meter, beside the tempo because it is the other thing that governs
        the whole project's time. A combo rather than two number fields: the
        denominator is a note value, so only five values are legal, and the pair
        reads as one thing - "4/4" - rather than as two numbers to reconcile.
    */
    juce::ComboBox meterBox;

    DewButton modeButton { "Pattern", DewButton::Role::normal };

    // Pattern management lives here because the pattern selector does: adding a
    // pattern used to be reachable only from a menu shortcut, which meant it
    // read as "you cannot add more patterns".
    juce::ComboBox patternBox;
    DewIconButton addPatternButton { icons::plus(), "Add a pattern" };
    DewIconButton clonePatternButton { icons::duplicate(), "Duplicate this pattern" };
    DewIconButton deletePatternButton { icons::trash(), "Delete this pattern" };
    DewNumberField patternLengthField;

    juce::Label positionLabel;

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
    struct MeterChoice { int beatsPerBar, beatUnit; };
    static const MeterChoice meterChoices[];
    static const int numMeterChoices;
    juce::Array<int> groupDividers;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBar)
};

} // namespace dew
