#include "ui/RenderPanel.h"

#include "engine/Sequencer.h"
#include "model/Ids.h"
#include "ui/design/Tokens.h"

namespace dew
{

using namespace tokens;

namespace
{

constexpr int rateIdBase = 1000; ///< sample rate in Hz IS the item id

/** The rates every format takes, and the two more that only uncompressed ones do. */
const juce::Array<int> allRates { 44100, 48000, 88200, 96000 };
const juce::Array<int> mp3Rates { 32000, 44100, 48000 };

juce::String describeSeconds (double seconds)
{
    if (seconds <= 0.0)
        return "-";

    const auto minutes = (int) (seconds / 60.0);
    const auto remainder = seconds - (double) minutes * 60.0;

    return juce::String (minutes) + ":" + juce::String (remainder, 1).paddedLeft ('0', 4);
}

} // namespace

RenderPanel::RenderPanel (ProjectDocument& d, EditorState& state, Settings* settingsToUpdate)
    : document (d)
    , editorState (state)
    , settings (settingsToUpdate)
{
    setComponentID ("renderPanel");
    setSize (preferredWidth, preferredHeight);

    for (auto* box : { &scopeBox, &formatBox, &rateBox, &depthBox, &mp3QualityBox })
        addAndMakeVisible (box);

    for (auto* toggle : { &normalizeToggle, &fadeToggle, &ditherToggle, &stemsToggle })
        addAndMakeVisible (toggle);

    addAndMakeVisible (tailField);
    addAndMakeVisible (peakField);
    addAndMakeVisible (renderButton);
    addAndMakeVisible (cancelButton);

    tailField.setRange (0.0, 30.0, 0.1);
    tailField.setNumDecimalPlaces (1);
    tailField.setSuffix (" s");
    tailField.setValue (1.0, juce::dontSendNotification);
    tailField.setTooltip ("Time rendered after the music ends, so release tails are not cut off");

    peakField.setRange (-24.0, 0.0, 0.1);
    peakField.setNumDecimalPlaces (1);
    peakField.setSuffix (" dB");
    peakField.setValue (-1.0, juce::dontSendNotification);
    peakField.setTooltip ("Where normalizing puts the loudest sample");

    depthBox.addItem ("16-bit", 16);
    depthBox.addItem ("24-bit", 24);
    depthBox.addItem ("32-bit float", 32);
    depthBox.setSelectedId (24, juce::dontSendNotification);

    ditherToggle.setToggleState (true, juce::dontSendNotification);
    ditherToggle.setTooltip (
        "Adds inaudible noise so 16-bit truncation does not distort quiet passages");

    rebuildFormats();
    rebuildScopes();

    // Restore what was asked for last time, validated the way Settings validates
    // everything else: an unusable value falls back rather than being restored.
    if (settings != nullptr)
    {
        const auto storedFormat = (RenderFormat) juce::jlimit (0, 3, settings->getRenderFormat());

        if (OfflineRenderer::isAvailable (storedFormat))
            formatBox.setSelectedId ((int) storedFormat + 1, juce::dontSendNotification);

        const auto storedRate = settings->getRenderSampleRate();

        if (allRates.contains (storedRate))
            rateBox.setSelectedId (rateIdBase + storedRate, juce::dontSendNotification);

        const auto storedDepth = settings->getRenderBitDepth();

        if (storedDepth == 16 || storedDepth == 24 || storedDepth == 32)
            depthBox.setSelectedId (storedDepth, juce::dontSendNotification);

        tailField.setValue (settings->getRenderTailSeconds(), juce::dontSendNotification);
        normalizeToggle.setToggleState (settings->getRenderNormalize(), juce::dontSendNotification);
    }

    const auto refresh = [this]
    {
        if (updating)
            return;

        updateVisibility();
        applyRequiredHeight();
        resized();
        updateSummary();
        repaint();
    };

    formatBox.onChange = [this, refresh]
    {
        if (updating)
            return;

        // The rate list depends on the format: mp3 takes three rates, and
        // offering 96kHz there would be offering a render that cannot be written.
        rebuildFormats();
        refresh();
    };

    for (auto* box : { &scopeBox, &rateBox, &depthBox, &mp3QualityBox })
        box->onChange = refresh;

    for (auto* toggle : { &normalizeToggle, &fadeToggle, &ditherToggle, &stemsToggle })
        toggle->onClick = refresh;

    tailField.onValueChange = refresh;
    peakField.onValueChange = refresh;

    renderButton.onClick = [this]
    {
        if (onRender != nullptr)
            onRender (getRequest());
    };

    cancelButton.onClick = [this]
    {
        if (onClose != nullptr)
            onClose();
    };

    rows = {
        { "SCOPE", &scopeBox }, { "FORMAT", &formatBox },      { "RATE", &rateBox },
        { "DEPTH", &depthBox }, { "QUALITY", &mp3QualityBox }, { "TAIL", &tailField },
        { "PEAK", &peakField }, { "", &normalizeToggle },      { "", &fadeToggle },
        { "", &ditherToggle },  { "", &stemsToggle },
    };

    editorState.addChangeListener (this);

    updateVisibility();
    setSize (preferredWidth, getRequiredHeight());
    updateSummary();
}

int RenderPanel::getRequiredHeight() const
{
    auto visibleRows = 0;

    for (const auto& row : rows)
        if (row.visible && row.control != nullptr)
            ++visibleRows;

    // Mirrors resized(), which is the only way the two can be trusted to agree.
    return space::xl * 2 + visibleRows * (size::controlHeight + space::sm) + space::md
           + size::controlHeight * 2 + space::lg + size::controlHeight;
}

void RenderPanel::applyRequiredHeight()
{
    const auto wanted = getRequiredHeight();

    if (wanted == getHeight())
        return;

    // The dialog owns the size; telling only the content leaves the window at
    // its old height with the content clipped or floating inside it.
    //
    // Unless the panel is being scrolled, in which case the window's height is
    // the SCREEN's business and not this panel's: growing it here is what put
    // the Render button off the bottom edge of a short display, on a dialog
    // that cannot be resized to bring it back.
    if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
    {
        if (findParentComponentOfClass<juce::Viewport>() != nullptr)
            setSize (getWidth(), wanted);
        else
            window->setContentComponentSize (getWidth(), wanted);
    }
    else
    {
        setSize (getWidth(), wanted);
    }
}

RenderPanel::~RenderPanel()
{
    editorState.removeChangeListener (this);
}

void RenderPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void RenderPanel::refresh()
{
    // The selection can come and go while this is open, so the Selection entry
    // has to come and go with it rather than sit there doing nothing.
    rebuildScopes();
    updateVisibility();
    applyRequiredHeight();
    resized();
    updateSummary();
    repaint();
}

RenderFormat RenderPanel::currentFormat() const
{
    return (RenderFormat) juce::jlimit (0, 3, formatBox.getSelectedId() - 1);
}

void RenderPanel::rebuildFormats()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);

    const auto wanted = formatBox.getSelectedId() > 0 ? formatBox.getSelectedId()
                                                      : (int) RenderFormat::wav + 1;

    formatBox.clear (juce::dontSendNotification);
    unavailableNote.clear();

    for (const auto format :
         { RenderFormat::wav, RenderFormat::flac, RenderFormat::mp3, RenderFormat::midi })
    {
        const auto id = (int) format + 1;
        formatBox.addItem (OfflineRenderer::nameFor (format), id);

        // Shown but not selectable, with the reason under the controls. Leaving
        // it out entirely would just raise the question of where MP3 went.
        if (! OfflineRenderer::isAvailable (format))
        {
            formatBox.setItemEnabled (id, false);
            unavailableNote = "MP3 needs the lame encoder. Install it with `brew install lame`.";
        }
    }

    // A format this machine cannot write must not end up selected, whether it
    // came from settings or from a lame that has since been uninstalled.
    const auto keep = OfflineRenderer::isAvailable ((RenderFormat) (wanted - 1));

    formatBox.setSelectedId (keep ? wanted : (int) RenderFormat::wav + 1,
                             juce::dontSendNotification);

    // The rate list narrows for mp3, so a rate that format cannot take does not
    // survive a switch to it.
    const auto rates = currentFormat() == RenderFormat::mp3 ? mp3Rates : allRates;
    const auto previous = rateBox.getSelectedId() - rateIdBase;

    rateBox.clear (juce::dontSendNotification);

    for (const auto rate : rates)
        rateBox.addItem (juce::String (rate) + " Hz", rateIdBase + rate);

    rateBox.setSelectedId (rateIdBase + (rates.contains (previous) ? previous : 44100),
                           juce::dontSendNotification);

    if (mp3QualityBox.getNumItems() == 0)
    {
        const auto qualities = OfflineRenderer::mp3QualityOptions();

        for (int i = 0; i < qualities.size(); ++i)
            mp3QualityBox.addItem (qualities[i], i + 1);

        mp3QualityBox.setSelectedId (juce::jmin (5, juce::jmax (1, qualities.size())),
                                     juce::dontSendNotification);
    }
}

void RenderPanel::rebuildScopes()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);

    const auto wanted = scopeBox.getSelectedId();

    scopeBox.clear (juce::dontSendNotification);
    scopeBox.addItem ("Song", songScope);
    scopeBox.addItem ("Pattern " + juce::String (editorState.getCurrentPatternId()), patternScope);

    if (editorState.hasBarSelection())
    {
        const auto selection = editorState.getSelectedBarRange();

        scopeBox.addItem ("Selection: bars " + juce::String (selection.getStart() + 1) + " to "
                              + juce::String (selection.getEnd()),
                          selectionScope);
    }

    const auto stillThere = wanted > 0 && scopeBox.indexOfItemId (wanted) >= 0;
    scopeBox.setSelectedId (stillThere ? wanted : songScope, juce::dontSendNotification);
}

void RenderPanel::updateVisibility()
{
    const auto format = currentFormat();
    const auto isMidi = format == RenderFormat::midi;
    const auto isMp3 = format == RenderFormat::mp3;
    const auto isFloat = depthBox.getSelectedId() == 32;

    for (auto& row : rows)
    {
        if (row.control == &scopeBox || row.control == &formatBox)
            row.visible = true;
        else if (row.control == &rateBox)
            row.visible = ! isMidi;
        else if (row.control == &depthBox)
            row.visible = ! isMidi && ! isMp3;
        else if (row.control == &mp3QualityBox)
            row.visible = isMp3;
        else if (row.control == &tailField)
            row.visible = ! isMidi;
        else if (row.control == &peakField)
            row.visible = ! isMidi && normalizeToggle.getToggleState();
        else if (row.control == &normalizeToggle)
            row.visible = ! isMidi;
        else if (row.control == &fadeToggle)
            row.visible = ! isMidi;
        else if (row.control == &ditherToggle)
            row.visible = ! isMidi && ! isFloat && depthBox.getSelectedId() == 16;
        else if (row.control == &stemsToggle)
            row.visible = ! isMidi;

        if (row.control != nullptr)
            row.control->setVisible (row.visible);
    }
}

void RenderPanel::updateSummary()
{
    const auto request = getRequest();

    const auto snapshot = buildSnapshot (document.getState(), nullptr);
    const auto patternIndex = snapshot.patternIndexForId (request.options.patternId);

    const auto materialSteps = Sequencer::materialLengthSteps (snapshot, request.options.mode,
                                                               patternIndex);

    // The snapshot's own map, which is already in seconds - so the summary of a
    // render with a tempo curve says how long it will actually be, rather than
    // how long it would be at the tempo the project starts at.
    const auto& map = *snapshot.tempoMap;

    auto seconds = map.secondsForSteps ((double) materialSteps);

    if (! request.options.barRange.isEmpty())
    {
        const auto stepsPerBar = (double) snapshot.stepsPerBar();

        seconds = map.secondsForSteps ((double) request.options.barRange.lastBar * stepsPerBar)
                  - map.secondsForSteps ((double) request.options.barRange.firstBar * stepsPerBar);
    }

    if (request.options.format != RenderFormat::midi)
        seconds += request.options.tailSeconds;

    if (request.options.format == RenderFormat::midi)
    {
        summaryText = describeSeconds (seconds) + " of notes, as one MIDI file";
        return;
    }

    summaryText = describeSeconds (seconds) + "  ·  "
                  + OfflineRenderer::nameFor (request.options.format) + "  ·  "
                  + juce::String (request.options.sampleRate, 0) + " Hz";

    // Note the shape of these appends. juce::String's constructor from a
    // const char* reads it as ASCII, while operator+= reads it as UTF-8 - so
    // `someString + "  ·  "` is right and `"  ·  " + someString` turns the
    // separator into two mojibake characters. Always append to a String.
    if (request.options.format == RenderFormat::wav || request.options.format == RenderFormat::flac)
    {
        summaryText += "  ·  ";
        summaryText += request.options.floatingPoint
                           ? juce::String ("32-bit float")
                           : juce::String (request.options.bitDepth) + "-bit";
    }

    if (request.stems)
    {
        // Worth saying out loud: this is N renders, not one.
        summaryText += "  ·  up to ";
        summaryText += juce::String ((int) snapshot.mixerTracks.size()) + " files";
    }
}

RenderPanel::Request RenderPanel::getRequest() const
{
    Request request;

    request.options.format = currentFormat();
    request.options.sampleRate = (double) (rateBox.getSelectedId() - rateIdBase);
    request.options.tailSeconds = tailField.getValue();
    request.options.normalize = normalizeToggle.getToggleState();
    request.options.normalizePeakDb = (float) peakField.getValue();
    request.options.dither = ditherToggle.getToggleState();
    request.options.mp3QualityIndex = juce::jmax (0, mp3QualityBox.getSelectedId() - 1);

    if (request.options.sampleRate <= 0.0)
        request.options.sampleRate = 44100.0;

    const auto depth = depthBox.getSelectedId();

    request.options.bitDepth = depth;
    request.options.floatingPoint = depth == 32;

    if (fadeToggle.getToggleState())
    {
        // Short: this is a declick, not a musical fade. A long one on a bounce
        // would be a surprise rather than a feature.
        request.options.fadeInSeconds = 0.01;
        request.options.fadeOutSeconds = 0.05;
    }

    switch (scopeBox.getSelectedId())
    {
        case patternScope:
            request.options.mode = Transport::Mode::pattern;
            request.options.patternId = editorState.getCurrentPatternId();
            break;

        case selectionScope:
        {
            const auto selection = editorState.getSelectedBarRange();

            request.options.mode = Transport::Mode::song;
            request.options.barRange = { selection.getStart(), selection.getEnd() };
            break;
        }

        case songScope:
        default: request.options.mode = Transport::Mode::song; break;
    }

    request.stems = stemsToggle.getToggleState() && request.options.format != RenderFormat::midi;

    auto name = document.getState()[ids::name].toString();

    if (name.isEmpty())
        name = "dew";

    if (request.options.mode == Transport::Mode::pattern)
        name += " pattern " + juce::String (request.options.patternId);
    else if (! request.options.barRange.isEmpty())
        name += " bars " + juce::String (request.options.barRange.firstBar + 1) + "-"
                + juce::String (request.options.barRange.lastBar);

    request.suggestedName = juce::File::createLegalFileName (name);

    return request;
}

bool RenderPanel::isRowVisible (const juce::String& label) const
{
    for (const auto& row : rows)
        if (row.label == label)
            return row.visible;

    return false;
}

void RenderPanel::setFormatForTesting (RenderFormat format)
{
    formatBox.setSelectedId ((int) format + 1, juce::sendNotificationSync);
}

void RenderPanel::setScopeForTesting (int itemId)
{
    scopeBox.setSelectedId (itemId, juce::sendNotificationSync);
}

void RenderPanel::resized()
{
    auto area = getLocalBounds().reduced (space::xl);

    // The footer first, off the bottom, so the rows above never run into it.
    auto footer = area.removeFromBottom (size::controlHeight);

    renderButton.setBounds (footer.removeFromRight (110));
    footer.removeFromRight (space::md);
    cancelButton.setBounds (footer.removeFromRight (90));

    area.removeFromBottom (space::lg);

    for (auto& row : rows)
    {
        if (! row.visible || row.control == nullptr)
        {
            row.labelBounds = {};
            continue;
        }

        auto line = area.removeFromTop (size::controlHeight);

        row.labelBounds = line.removeFromLeft (74);
        line.removeFromLeft (space::md);

        // A toggle carries its own text, so it gets the whole row.
        row.control->setBounds (row.label.isEmpty() ? row.labelBounds.getUnion (line) : line);

        area.removeFromTop (space::sm);
    }

    // Directly under the rows, so the panel does not carry a block of empty
    // space whenever the chosen format needs fewer of them.
    area.removeFromTop (space::md);
    summaryBounds = area.removeFromTop (size::controlHeight * 2);
}

void RenderPanel::paint (juce::Graphics& g)
{
    g.fillAll (colour::background);

    g.setFont (type::font (type::caption));
    g.setColour (colour::textSecondary);

    for (const auto& row : rows)
        if (row.visible && row.label.isNotEmpty() && ! row.labelBounds.isEmpty())
            g.drawText (row.label, row.labelBounds, juce::Justification::centredLeft, false);

    // The live state, under the controls: what the choices above add up to.
    auto summary = summaryBounds;

    g.setColour (colour::textPrimary);
    g.setFont (type::font (type::small));
    g.drawText (summaryText, summary.removeFromTop (size::controlHeight),
                juce::Justification::centredLeft, true);

    if (unavailableNote.isNotEmpty())
    {
        g.setColour (colour::textSecondary);
        g.setFont (type::font (type::caption));
        g.drawText (unavailableNote, summary, juce::Justification::centredLeft, true);
    }
}

} // namespace dew
