#include "ui/primitives/DewPaint.h"
#include "ui/TransportBar.h"

#include "ui/TimeText.h"

#include <memory>
#include <utility>

#include "i18n/Strings.h"
#include "model/ModuleCatalog.h"

#include "model/Ids.h"
#include "model/Meter.h"
#include "model/ProjectEdits.h"
#include "ui/design/DewLookAndFeel.h"
#include "ui/design/Glyphs.h"
#include "ui/design/Icons.h"
#include "ui/design/MenuGlyph.h"
#include "ui/StripLayout.h"
#include "ui/design/Tokens.h"

namespace dew
{

TransportBar::TransportBar (ProjectDocument& d, AudioEngine& e, EditorState& s)
    : document (d)
    , engine (e)
    , editorState (s)
    , signalScope (&e)
{
    setComponentID ("transportBar");

    // Twelve controls that belonged to no group at all: play, stop, record, the
    // tempo, the metre, the pattern picker and its four buttons. The nearest
    // focus container above them was the top-level component, which is not one -
    // so a screen reader met them as a loose run and ctrl-tab had nowhere to
    // start from.
    setTitle (tr (StringId::transport_title));
    setFocusContainerType (FocusContainerType::focusContainer);

    confirmDestructive = confirmWithPanel (this);

    patternBox.setComponentID ("patternSelector");
    patternBox.setTooltip (tr (StringId::transport_pattern_help));

    // A pattern name says nothing about being a pattern - "Groove" beside a
    // tempo and a time signature reads as one more setting.
    styleCaption (patternCaption, StringId::transport_pattern_caption);
    addAndMakeVisible (patternCaption);

    playButton.onClick = [this]
    {
        if (engine.isPlaying())
            engine.stop();
        else
            engine.play();
    };
    addAndMakeVisible (playButton);

    stopButton.onClick = [this]
    {
        engine.stop();
        engine.rewind();
    };
    addAndMakeVisible (stopButton);

    // Neither of those writes the icon. It is POLLED - see timerCallback.

    // No setOnColour here any more: the fill an armed record button crosses to
    // is what DewIconButton::Role::record MEANS, so the role carries it. It was
    // stated here, one line after naming the role that already implied it.
    recordButton.onClick = [this]
    {
        if (onToggleRecord != nullptr)
            onToggleRecord();
    };
    addAndMakeVisible (recordButton);

    // The click and the typing keyboard - see TransportBarToggles.cpp.
    createToggles();

    // AFTER record in the child order, deliberately: TransportBarTests reads
    // the play button as the first DewIconButton in the bar, and a panic put in
    // front of it would silently make every one of those tests about the wrong
    // control.
    panicButton.onClick = [this]
    {
        engine.panic();

        if (onPanic != nullptr)
            onPanic();
    };
    addAndMakeVisible (panicButton);

    // From the catalog, not by hand. This field stated 20..300 while the engine
    // clamped at 20..999 - a fourth disagreement of exactly the kind the spec
    // tables exist to end, and one that made the top two thirds of what dew can
    // actually render impossible to type in.
    const auto& tempoSpec = projectParamSpecs().front();

    tempoField.setRange (tempoSpec.minimum, tempoSpec.maximum, tempoSpec.interval);
    tempoField.setNumDecimalPlaces (tempoSpec.decimals);
    tempoField.setSuffix (tempoSpec.suffix);
    tempoField.setLogarithmic (tempoSpec.curve == ParamCurve::logarithmic);
    tempoField.setTooltip (tr (StringId::transport_tempo_help));
    tempoField.onEditStart = [this] { tempoGestureActive = false; };
    tempoField.onValueChange = [this]
    {
        ProjectEdits::setProperty (document.getState(), ids::tempoBpm, tempoField.getValue(),
                                   &document.getUndoManager(), "Change tempo", tempoGestureActive);

        // A number field drag emits a value per frame, exactly as a knob does.
        tempoGestureActive = true;
    };
    addAndMakeVisible (tempoField);

    rebuildMeterList();
    rebuildGridList();
    meterBox.setComponentID ("timeSignature");
    meterBox.setTooltip (tr (StringId::transport_meter_help));
    meterBox.onChange = [this]
    {
        if (updatingMeterBox)
            return;

        applyMeterChoice (meterBox.getSelectedId());
    };
    addAndMakeVisible (meterBox);

    gridBox.setComponentID ("gridResolution");
    gridBox.setTooltip (tr (StringId::transport_grid_help));
    gridBox.onChange = [this]
    {
        if (updatingGridBox)
            return;

        applyGridChoice (gridBox.getSelectedId());
    };
    addAndMakeVisible (gridBox);

    modeButton.setTooltip (tr (StringId::transport_mode_help));
    modeButton.setClickingTogglesState (true);
    modeButton.onClick = [this]
    {
        // Tells the ENGINE and nothing else. The caption and the fill follow
        // from refreshEngineState, which is the one reader - two writers, one
        // of which every other route bypassed, is exactly how the button came
        // to disagree with the transport.
        engine.setMode (modeButton.getToggleState() ? Transport::Mode::song
                                                    : Transport::Mode::pattern);
        engine.rewind();
    };
    addAndMakeVisible (modeButton);

    patternBox.onChange = [this]
    {
        if (updatingPatternList)
            return;

        const auto id = patternBox.getSelectedId();

        // The list ends in "New pattern", so the dropdown someone opens looking
        // for their patterns is also where a new one comes from - the + beside
        // it is easy to miss when the box is what you were already looking at.
        if (id == newPatternItemId)
        {
            addPattern();
            return;
        }

        if (id > 0)
        {
            editorState.setCurrentPatternId (id);
            engine.setCurrentPatternId (id);
        }
    };
    addAndMakeVisible (patternBox);

    addPatternButton.onClick = [this] { addPattern(); };
    addAndMakeVisible (addPatternButton);

    clonePatternButton.onClick = [this]
    {
        auto& undo = document.getUndoManager();
        undo.beginNewTransaction ("Duplicate pattern");
        const auto copy = ProjectEdits::duplicatePattern (document.getState(), currentPattern(),
                                                          &undo);

        if (copy.isValid())
        {
            editorState.setCurrentPatternId ((int) copy[ids::id]);
            engine.setCurrentPatternId ((int) copy[ids::id]);
        }
    };
    addAndMakeVisible (clonePatternButton);

    deletePatternButton.setComponentID ("deletePattern");
    deletePatternButton.onClick = [this] { requestDeletePattern(); };
    addAndMakeVisible (deletePatternButton);

    positionLabel.setComponentID ("transportPosition");
    positionLabel.setFont (tokens::type::monospaced (tokens::type::body));
    positionLabel.setTextColourToken (tokens::colour::playhead);
    positionLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (positionLabel);

    elapsedLabel.setComponentID ("transportElapsed");
    elapsedLabel.setFont (tokens::type::monospaced (tokens::type::small));
    elapsedLabel.setTextColourToken (tokens::colour::textSecondary);
    elapsedLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (elapsedLabel);

    addAndMakeVisible (signalScope);

    document.getState().addListener (this);
    editorState.addChangeListener (this);

    refresh();
    startTimerHz (tokens::motion::uiRefreshHz);
}

TransportBar::~TransportBar()
{
    document.getState().removeListener (this);
    editorState.removeChangeListener (this);
}

void TransportBar::setParamMenuHost (const paramMenu::Host* host)
{
    paramMenuHost = host;

    // Through the field's own hook rather than a Trigger: a DewNumberField has
    // one, and only the raw juce::Sliders in the mixer and the instrument panel
    // need the listener form.
    paramMenu::attachTo (
        host, tempoField, [this] { return document.getState(); }, projectParamSpecs().front());
}

void TransportBar::refresh()
{
    tempoField.setValue ((double) document.getState()[ids::tempoBpm], juce::dontSendNotification);
    refreshMeter();
    refreshGrid();
    rebuildPatternList();
    refreshPatternControls();

    // Otherwise the readout, the icon and the mode are blank and stale until
    // the first timer tick. The same call the timer makes, so a document
    // replace and a tick cannot come to disagree about what the engine says.
    refreshEngineState();
    updatePositionLabel();
}

juce::ValueTree TransportBar::currentPattern() const
{
    return ProjectEdits::findPattern (document.getState(), editorState.getCurrentPatternId());
}

int TransportBar::countPatterns() const
{
    int n = 0;

    for (const auto& child : document.getState())
        if (child.hasType (ids::PATTERN))
            ++n;

    return n;
}

void TransportBar::requestDeletePattern()
{
    const auto pattern = currentPattern();

    if (! pattern.isValid() || countPatterns() <= 1)
        return;

    ConfirmPanel::Request request;
    request.title = tr (StringId::dialog_deletePattern_title);
    request.message = tr (StringId::dialog_deletePattern_body,
                          Args {}.with ("name", pattern[ids::name].toString()));

    // By ID, resolved again when the answer comes back: a dialog is async and
    // the document is free to change while it is open, so a captured ValueTree
    // would be a node that may no longer be in the project.
    confirmDestructive (request,
                        [this, patternId = (int) pattern[ids::id]]
                        {
                            auto& undo = document.getUndoManager();
                            undo.beginNewTransaction ("Delete pattern");
                            ProjectEdits::removePattern (
                                document.getState(),
                                ProjectEdits::findPattern (document.getState(), patternId), &undo);
                        });
}

void TransportBar::refreshPatternControls()
{
    // Deleting the only pattern would leave nothing to edit or play.
    // Counted from the DOCUMENT, not from the box. The box carries a "New
    // pattern" row below a separator, so with one pattern getNumItems() came
    // back 2 - the button was enabled, the click fired, and removePattern
    // refused. A button that silently does nothing, and a confirmation on top
    // of it would have made it worse: "Delete Pattern 1?" then nothing.
    deletePatternButton.setEnabled (countPatterns() > 1);
}

void TransportBar::addPattern()
{
    // One code path for the + button and the menu item. Two would be two places
    // to remember that a new pattern is also the pattern to switch to.
    auto& undo = document.getUndoManager();
    undo.beginNewTransaction ("Add pattern");

    const auto pattern = ProjectEdits::addPattern (document.getState(), &undo);

    editorState.setCurrentPatternId ((int) pattern[ids::id]);
    engine.setCurrentPatternId ((int) pattern[ids::id]);
}

void TransportBar::setCurrentPattern (int wantedId)
{
    auto id = wantedId;

    if (! ProjectEdits::findPattern (document.getState(), id).isValid())
    {
        // The first pattern the PROJECT has. A project with none leaves the id
        // alone rather than inventing one: there is nothing to open, and the
        // dropdown's first row is the "New pattern" sentinel.
        id = 0;

        for (const auto& pattern : document.getState())
        {
            if (! pattern.hasType (ids::PATTERN))
                continue;

            id = (int) pattern[ids::id];
            break;
        }

        if (id == 0)
            return;
    }

    editorState.setCurrentPatternId (id);
    engine.setCurrentPatternId (id);

    if (patternBox.indexOfItemId (id) >= 0)
        patternBox.setSelectedId (id, juce::dontSendNotification);
}

void TransportBar::rebuildPatternList()
{
    const juce::ScopedValueSetter<bool> quiet (updatingPatternList, true);

    patternBox.clear (juce::dontSendNotification);

    for (const auto& pattern : document.getState())
        if (pattern.hasType (ids::PATTERN))
            addGlyphItem (patternBox, (int) pattern[ids::id], pattern[ids::name].toString(),
                          icons::pattern());

    // Well above any pattern id, so it can never collide with one. Glyphed
    // too, and with a DIFFERENT glyph: the last row is the one that makes a
    // pattern rather than one that opens one, and the separator alone said so
    // only to someone already looking for it.
    patternBox.addSeparator();
    addGlyphItem (patternBox, newPatternItemId, tr (StringId::transport_newPattern),
                  glyph::Action::add);

    setCurrentPattern (editorState.getCurrentPatternId());
    patternBox.setSelectedId (editorState.getCurrentPatternId(), juce::dontSendNotification);
}

void TransportBar::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (patternBox.getSelectedId() != editorState.getCurrentPatternId())
        patternBox.setSelectedId (editorState.getCurrentPatternId(), juce::dontSendNotification);

    refreshPatternControls();
}

void TransportBar::valueTreePropertyChanged (juce::ValueTree& tree,
                                             const juce::Identifier& property)
{
    if (property == ids::tempoBpm)
        tempoField.setValue ((double) tree[ids::tempoBpm], juce::dontSendNotification);
    else if ((property == ids::beatsPerBar || property == ids::beatUnit)
             && tree.hasType (ids::PROJECT))
    {
        refreshMeter();

        // The metre names the grid too: "1/16" is a sixteenth only while a beat
        // is a quarter, and the list says so in the notation of the project it
        // is in.
        rebuildGridList();
        refreshGrid();
    }
    else if (property == ids::stepsPerBeat && tree.hasType (ids::PROJECT))
        refreshGrid();
    else if (property == ids::name && tree.hasType (ids::PATTERN))
        rebuildPatternList();
}

void TransportBar::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child)
{
    if (child.hasType (ids::PATTERN))
    {
        rebuildPatternList();
        refreshPatternControls();
    }
}

void TransportBar::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& child, int)
{
    if (child.hasType (ids::PATTERN))
    {
        rebuildPatternList();
        refreshPatternControls();
    }
}

void TransportBar::timerCallback()
{
    updatePositionLabel();
    refreshEngineState();
}

void TransportBar::refreshEngineState()
{
    refreshPlayIcon();

    // Polled with the playhead rather than pushed: a take can also end from the
    // menu, from the keyboard, or because the device went away, and this is
    // already the thing running at the rate a transport reads at.
    if (isRecording != nullptr)
    {
        const auto recording = isRecording() ? 1 : 0;

        if (std::exchange (showingRecording, recording) != recording)
            recordButton.setToggleState (recording != 0, juce::dontSendNotification);
    }

    refreshToggles();

    // The mode, which was the one engine-owned control nobody polled.
    //
    // refresh() set it, and refresh() runs when the document is REPLACED - so
    // cmd-L and the Transport menu both flipped the engine while the button
    // kept a stale fill AND a stale caption until something unrelated happened
    // to open a project. The click handler wrote both halves itself, which is
    // what hid it: the only route anybody tested was the one route that also
    // updated the button.
    const auto song = engine.getMode() == Transport::Mode::song ? 1 : 0;

    if (std::exchange (showingSongMode, song) == song)
        return;

    modeButton.setToggleState (song != 0, juce::dontSendNotification);
    modeButton.setButtonText (tr (song != 0 ? StringId::transport_modeSong_label
                                            : StringId::transport_modePattern_label));
}

void TransportBar::refreshPlayIcon()
{
    /*  Polled, for exactly the reason the record button above it is.

        The icon used to be written by the three things that could change it
        from HERE - its own click, the stop button's, and refresh() - and
        AudioEngine is not a ChangeBroadcaster, so nothing told this bar when
        the transport moved any other way. Space, the Transport menu and an MCP
        client all left a playing transport showing a play triangle, until
        something unrelated happened to call refresh().

        Latched, because this runs at motion::uiRefreshHz and setIcon repaints.
    */
    const auto playing = engine.isPlaying();

    if (std::exchange (showingPause, playing) == playing)
        return;

    playButton.setIcon (playing ? icons::pause() : icons::play());
}

juce::String TransportBar::positionText (double steps, const Meter& meter)
{
    const auto stepsPerBeat = juce::jmax (1, meter.stepsPerBeat);
    const auto stepsPerBar = juce::jmax (1, meter.stepsPerBar());

    const auto totalSteps = juce::jmax (0, (int) steps);
    const auto bar = totalSteps / stepsPerBar + 1;
    const auto beat = (totalSteps % stepsPerBar) / stepsPerBeat + 1;
    const auto tick = totalSteps % stepsPerBeat + 1;

    return juce::String::formatted ("%03d:%d:%d", bar, beat, tick);
}

void TransportBar::updatePositionLabel()
{
    positionLabel.setText (
        positionText (engine.getPlayheadSteps(), Meter::of (document.getState())),
        juce::dontSendNotification);

    // Off the engine's own seconds rather than off the steps above: dividing a
    // step count by a tempo is right only while that tempo is the whole song,
    // and this would be quietly wrong on every project with a tempo curve.
    elapsedLabel.setText (timeText::clock (engine.getPlayheadSeconds()),
                          juce::dontSendNotification);
}

void TransportBar::paint (juce::Graphics& g)
{
    g.fillAll (tokens::colour::surface);

    g.setColour (tokens::colour::dividerStrong);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    // Separators between the transport, the pattern controls and the readout,
    // down the band the controls themselves occupy.
    for (auto x : groupDividers)
    {
        g.setColour (tokens::colour::divider);
        g.drawVerticalLine (x, (float) controlBand.getY(), (float) controlBand.getBottom());
    }
}

void TransportBar::resized()
{
    using namespace tokens;

    groupDividers.clear();

    StripLayout strip { getLocalBounds(), space::md, space::sm };

    controlBand = strip.band();

    const auto place = [&strip] (juce::Component& c, int width) { strip.place (c, width); };

    // Panic's slot, taken off the right end BEFORE the chain below runs, and
    // one gap wider than the button so the scope has something to sit against.
    //
    // Reserved rather than laid out in the leftovers because the window's own
    // minimum width is 900 and at 900 the chain below fills the strip: a panic
    // button placed last is a panic button that is not there when the window is
    // small, which is exactly when somebody reaches for it. WHERE it is drawn
    // is settled at the bottom, once the scope has said whether it is there.
    const auto reserved = strip.placeFromRight (30 + space::sm);

    place (playButton, 30);
    place (stopButton, 30);
    place (recordButton, 30);

    groupDividers.add (strip.divider());

    // Their own category, behind a rule rather than butted onto the transport
    // run. Neither is a transport control: one is a thing you hear and the
    // other a thing you play, and both are modes that stay on rather than
    // actions that happen once.
    place (metronomeButton, 30);
    place (keyboardButton, 30);

    groupDividers.add (strip.divider());

    place (tempoField, 96);
    place (meterBox, 72);

    // Wider than the metre beside it: "4/4" is three characters and a grid can
    // read "1/32 T", which is six and was an ellipsis at the metre's width.
    place (gridBox, 88);
    strip.gap();
    place (modeButton, 78);

    groupDividers.add (strip.divider());

    place (patternCaption, tokens::size::gutterLabel - tokens::space::lg);
    place (patternBox, 148);
    place (addPatternButton, size::knobSm);
    place (clonePatternButton, size::knobSm);
    place (deletePatternButton, size::knobSm);

    groupDividers.add (strip.divider());

    place (positionLabel, 84);
    place (elapsedLabel, 56);

    // The scope is taken from the right rather than as the next link in the
    // chain above: everything before it is a control with a width it needs, and
    // this is the only thing in the bar that should give way when the window
    // does. Hidden rather than squeezed, because a sixty-pixel oscilloscope is
    // not a smaller oscilloscope, it is noise.
    const auto roomForScope = strip.getRemainingWidth() >= SignalScope::preferredWidth + space::lg;
    signalScope.setVisible (roomForScope);

    // Panic is away from the transport run for the reason it always was - it is
    // not a fourth transport button and a hand reaching for stop must not find
    // it - and now at the right end, where nothing sits beside it to be hit by
    // accident.
    const auto end = reserved.getRight();

    if (! roomForScope)
    {
        panicButton.setBounds (reserved.withWidth (30).withX (end - 30));
        return;
    }

    // The scope takes the very end and panic the slot immediately inside it:
    // the order the two are wanted in, which is the reverse of the order a
    // strip laid out from the right had to reserve them in. The two spans add
    // up to exactly what was reserved plus what the scope has just asked for,
    // so nothing here reaches back over the chain above.
    const auto slot = strip.placeFromRight (SignalScope::preferredWidth);
    const auto scopeX = end - SignalScope::preferredWidth;

    signalScope.setBounds (slot.withX (scopeX));
    panicButton.setBounds (reserved.withWidth (30).withX (scopeX - space::sm - 30));

    groupDividers.add (strip.dividerFromRight());
}

} // namespace dew
