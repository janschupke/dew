#include "ui/TransportBar.h"

#include <memory>

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

// Simple metres first, then compound, then the odd ones - the order a musician
// would look for them in rather than numeric order. Every denominator here is a
// note value, which is why the list is closed: 3/6 is not a metre.
const TransportBar::MeterChoice TransportBar::meterChoices[] = { { 4, 4 }, { 3, 4 },  { 2, 4 },
                                                                 { 5, 4 }, { 6, 4 },  { 6, 8 },
                                                                 { 9, 8 }, { 12, 8 }, { 7, 8 },
                                                                 { 5, 8 }, { 2, 2 },  { 3, 8 } };

const int TransportBar::numMeterChoices = (int) (sizeof (meterChoices) / sizeof (meterChoices[0]));

TransportBar::TransportBar (ProjectDocument& d, AudioEngine& e, EditorState& s)
    : document (d)
    , engine (e)
    , editorState (s)
    , signalScope (&e)
{
    setComponentID ("transportBar");

    confirmDestructive = confirmWithPanel (this);

    patternBox.setComponentID ("patternSelector");
    patternBox.setTooltip (tr (StringId::transport_pattern_help));

    // A pattern name says nothing about being a pattern - "Groove" beside a
    // tempo and a time signature reads as one more setting.
    styleCaption (patternCaption, "PATTERN");
    addAndMakeVisible (patternCaption);

    playButton.onClick = [this]
    {
        if (engine.isPlaying())
            engine.stop();
        else
            engine.play();

        playButton.setIcon (engine.isPlaying() ? icons::pause() : icons::play());
    };
    addAndMakeVisible (playButton);

    stopButton.onClick = [this]
    {
        engine.stop();
        engine.rewind();
        playButton.setIcon (icons::play());
    };
    addAndMakeVisible (stopButton);

    // The icon and the colour have existed in the design system since before
    // anything could record; this is their first call site.
    recordButton.setOnColour (tokens::colour::recording);
    recordButton.onClick = [this]
    {
        if (onToggleRecord != nullptr)
            onToggleRecord();
    };
    addAndMakeVisible (recordButton);

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
    meterBox.setComponentID ("timeSignature");
    meterBox.setTooltip (tr (StringId::transport_meter_help));
    meterBox.onChange = [this]
    {
        if (updatingMeterBox)
            return;

        applyMeterChoice (meterBox.getSelectedId());
    };
    addAndMakeVisible (meterBox);

    modeButton.setTooltip (tr (StringId::transport_mode_help));
    modeButton.setClickingTogglesState (true);
    modeButton.onClick = [this]
    {
        const auto song = modeButton.getToggleState();
        modeButton.setButtonText (song ? "Song" : "Pattern");
        engine.setMode (song ? Transport::Mode::song : Transport::Mode::pattern);
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

    patternLengthField.setRange (1.0, 256.0, 1.0);
    patternLengthField.setNumDecimalPlaces (0);
    // A suffix rather than a caption, matching the tempo field beside it: a
    // caption reserves 12px above the value, which made this the only control
    // in the bar that could not share the common height.
    patternLengthField.setSuffix (tr (StringId::unit_steps));
    patternLengthField.setTooltip (tr (StringId::transport_patternLength_help));
    patternLengthField.onEditStart = [this] { lengthGestureActive = false; };
    patternLengthField.onValueChange = [this]
    {
        ProjectEdits::setProperty (currentPattern(), ids::lengthSteps,
                                   (int) patternLengthField.getValue(), &document.getUndoManager(),
                                   "Change pattern length", lengthGestureActive);

        lengthGestureActive = true;
    };
    addAndMakeVisible (patternLengthField);

    positionLabel.setComponentID ("transportPosition");
    positionLabel.setFont (tokens::type::monospaced (tokens::type::body));
    positionLabel.setTextColourToken (tokens::colour::playhead);
    positionLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (positionLabel);

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
    rebuildPatternList();
    refreshPatternLength();

    const auto song = engine.getMode() == Transport::Mode::song;
    modeButton.setToggleState (song, juce::dontSendNotification);
    modeButton.setButtonText (song ? "Song" : "Pattern");
    playButton.setIcon (engine.isPlaying() ? icons::pause() : icons::play());

    // Otherwise the readout is blank until the first timer tick.
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

void TransportBar::refreshPatternLength()
{
    const auto pattern = currentPattern();

    patternLengthField.setEnabled (pattern.isValid());

    if (pattern.isValid())
        patternLengthField.setValue ((double) (int) pattern[ids::lengthSteps],
                                     juce::dontSendNotification);

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
                  glyph::forAction (glyph::Action::add));

    setCurrentPattern (editorState.getCurrentPatternId());
    patternBox.setSelectedId (editorState.getCurrentPatternId(), juce::dontSendNotification);
}

void TransportBar::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (patternBox.getSelectedId() != editorState.getCurrentPatternId())
        patternBox.setSelectedId (editorState.getCurrentPatternId(), juce::dontSendNotification);

    refreshPatternLength();
}

void TransportBar::valueTreePropertyChanged (juce::ValueTree& tree,
                                             const juce::Identifier& property)
{
    if (property == ids::tempoBpm)
        tempoField.setValue ((double) tree[ids::tempoBpm], juce::dontSendNotification);
    else if ((property == ids::beatsPerBar || property == ids::beatUnit)
             && tree.hasType (ids::PROJECT))
        refreshMeter();
    else if (property == ids::name && tree.hasType (ids::PATTERN))
        rebuildPatternList();
    else if (property == ids::lengthSteps && tree.hasType (ids::PATTERN))
        refreshPatternLength();
}

void TransportBar::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child)
{
    if (child.hasType (ids::PATTERN))
    {
        rebuildPatternList();
        refreshPatternLength();
    }
}

void TransportBar::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& child, int)
{
    if (child.hasType (ids::PATTERN))
    {
        rebuildPatternList();
        refreshPatternLength();
    }
}

void TransportBar::timerCallback()
{
    updatePositionLabel();

    // Polled with the playhead rather than pushed: a take can also end from the
    // menu, from the keyboard, or because the device went away, and this is
    // already the thing running at the rate a transport reads at.
    if (isRecording != nullptr)
        recordButton.setToggleState (isRecording(), juce::dontSendNotification);
}

void TransportBar::rebuildMeterList()
{
    const juce::ScopedValueSetter<bool> guard (updatingMeterBox, true);

    meterBox.clear (juce::dontSendNotification);

    // Ids are one-based because a ComboBox reads 0 as "nothing selected".
    for (int i = 0; i < numMeterChoices; ++i)
    {
        const auto& choice = meterChoices[i];
        meterBox.addItem (
            tr (StringId::transport_meter_format,
                Args {}.with ("beats", choice.beatsPerBar).with ("unit", choice.beatUnit)),
            i + 1);
    }
}

void TransportBar::refreshMeter()
{
    const juce::ScopedValueSetter<bool> guard (updatingMeterBox, true);

    const auto meter = Meter::of (document.getState());

    for (int i = 0; i < numMeterChoices; ++i)
    {
        if (meterChoices[i].beatsPerBar == meter.beatsPerBar
            && meterChoices[i].beatUnit == meter.beatUnit)
        {
            meterBox.setSelectedId (i + 1, juce::dontSendNotification);
            return;
        }
    }

    // A metre the list does not offer - a file written by hand, or by a later
    // build. Shown as itself rather than snapped to the nearest entry, because
    // the box would otherwise claim the project is something it is not.
    meterBox.addItem (meter.toString(), numMeterChoices + 1);
    meterBox.setSelectedId (numMeterChoices + 1, juce::dontSendNotification);
}

void TransportBar::applyMeterChoice (int itemId)
{
    const auto index = itemId - 1;

    if (index < 0 || index >= numMeterChoices)
        return;

    const auto& choice = meterChoices[index];

    // One transaction for the metre and the rescale it drags behind it, so undo
    // puts the whole arrangement back rather than half of it.
    auto& undo = document.getUndoManager();
    undo.beginNewTransaction ("Change time signature");

    auto exact = true;
    ProjectEdits::setMeter (document.getState(), choice.beatsPerBar, choice.beatUnit, &undo,
                            &exact);

    if (onMeterChanged != nullptr)
        onMeterChanged (exact);
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

    place (playButton, 30);
    place (stopButton, 30);
    place (recordButton, 30);
    strip.gap();
    place (tempoField, 96);
    place (meterBox, 72);
    strip.gap();
    place (modeButton, 78);

    groupDividers.add (strip.divider());

    place (patternCaption, tokens::size::gutterLabel - tokens::space::lg);
    place (patternBox, 148);
    place (addPatternButton, size::knobSm);
    place (clonePatternButton, size::knobSm);
    place (deletePatternButton, size::knobSm);
    place (patternLengthField, 84);

    groupDividers.add (strip.divider());

    place (positionLabel, 84);

    // Taken from the right rather than as the next link in the chain above:
    // everything before it is a control with a width it needs, and this is the
    // only thing in the bar that should give way when the window does. Hidden
    // rather than squeezed, because a sixty-pixel oscilloscope is not a smaller
    // oscilloscope, it is noise.
    const auto roomForScope = strip.getRemainingWidth() >= SignalScope::preferredWidth + space::lg;
    signalScope.setVisible (roomForScope);

    if (roomForScope)
    {
        signalScope.setBounds (strip.placeFromRight (SignalScope::preferredWidth));
        groupDividers.add (strip.dividerFromRight());
    }
}

} // namespace dew
