#include "ui/MenuSeam.h"
#include "ui/InstrumentPanel.h"

#include "i18n/Strings.h"
#include "model/PresetLibrary.h"

#include <cmath>

#include "engine/EngineSnapshot.h"
#include "model/Ids.h"
#include "model/ModuleCatalog.h"
#include "model/ProjectEdits.h"
#include "ui/design/Cursors.h"
#include "ui/design/DewLookAndFeel.h"
#include "ui/design/Glyphs.h"
#include "ui/design/Icons.h"
#include "ui/design/MenuGlyph.h"

namespace dew
{

using namespace tokens;

namespace
{

} // namespace

InstrumentPanel::InstrumentPanel (ProjectDocument& d, EditorState& s, SamplePool* pool,
                                  SoundFontPool* soundFonts)
    : document (d)
    , editorState (s)
    , oscSection (d, s)
    , sampleSection (d, pool)
    , soundFontSection (d, soundFonts)
    , chainHost (d, s, EffectChainHost::Orientation::vertical)
{
    addChildComponent (sampleSection);
    addChildComponent (soundFontSection);

    setComponentID ("instrumentPanel");
    // A name and a PLACE in the tree a screen reader is given. focusContainer,
    // not keyboardFocusContainer: the two flags are independent, and the second
    // would confine the tab key to this panel with no key to leave it - a
    // keyboard trap, which is worse than the flat tab order it would tidy.
    setTitle (tr (StringId::instrument_title));
    setFocusContainerType (FocusContainerType::focusContainer);

    addAndMakeVisible (chainHost);

    // A card opening changes the chain's height, which changes the panel's,
    // which is what the sidebar's viewport scrolls. Passed straight up rather
    // than answered here: this panel does not own the scroller.
    chainHost.onPreferredHeightChanged = [this]
    {
        resized();

        if (onRequiredHeightChanged != nullptr)
            onRequiredHeightChanged();
    };

    titleLabel.setFont (tokens::type::font (tokens::type::title, true));
    addAndMakeVisible (titleLabel);

    presetButton.setTooltip (tr (StringId::instrument_preset_help));
    presetButton.onClick = [this] { showPresetMenu(); };
    addAndMakeVisible (presetButton);

    collapseButton.setComponentID ("instrumentCollapse");
    collapseButton.onClick = [this]
    { editorState.setInstrumentExpanded (! isInstrumentExpanded()); };
    addAndMakeVisible (collapseButton);

    // The oscillator section's height depends on the mode of the slot it is
    // showing, and that mode lives on an OSC node this panel does not listen to
    // - by design, since a change to another channel's oscillator is none of
    // its business. So the section says when its height moved.
    oscSection.onHeightChanged = [this] { resized(); };
    addAndMakeVisible (oscSection);

    const auto ampOf = [this]
    { return selectedChannel().getChildWithName (ids::INSTRUMENT).getChildWithName (ids::AMP); };
    const auto channelOf = [this] { return selectedChannel(); };

    attachKnob (attackKnob, ampOf, ids::attack, "Change attack");
    attachKnob (decayKnob, ampOf, ids::decay, "Change decay");
    attachKnob (sustainKnob, ampOf, ids::sustain, "Change sustain");
    attachKnob (releaseKnob, ampOf, ids::release, "Change release");

    attachKnob (volumeKnob, channelOf, ids::volume, "Change volume");
    attachKnob (panKnob, channelOf, ids::pan, "Change pan");

    editorState.addChangeListener (this);
    document.getState().addListener (this);

    refresh();
}

InstrumentPanel::~InstrumentPanel()
{
    editorState.removeChangeListener (this);
    document.getState().removeListener (this);
}

// The log-skew helper that used to live here has gone with the sliders it
// served: DewKnob works it out from the ParamSpec's curve, once, for every knob
// in the application rather than for these six.

void InstrumentPanel::setPresetHoverSink (std::function<void (const juce::String&)> sink)
{
    // Both menus this panel can open: the channel's own presets, and the
    // presets of every effect in the chain underneath them.
    chainHost.setPresetHoverSink (sink);
    onPresetHover = std::move (sink);
}

void InstrumentPanel::setParamMenuHost (const paramMenu::Host* host)
{
    paramMenuHost = host;

    // The chain under the panel gets it too: an effect's knobs are the largest
    // group of spec-built controls in the application, and the panel is the only
    // thing between them and whoever owns the host.
    chainHost.getChain().setParamMenuHost (host);
    oscSection.setParamMenuHost (host);
    sampleSection.setParamMenuHost (host);
    soundFontSection.setParamMenuHost (host);

    paramMenuTriggers.clear();

    if (host == nullptr || host->document == nullptr)
        return;

    // The rotaries are attached in the constructor, before the host arrives, so
    // this goes back over them rather than only recording the pointer.
    for (const auto& bound : boundRotaries)
    {
        const auto& spec = requireInstrumentParamSpec (bound.property);

        // A DewKnob has its own hook, and it has to be the thing that gets it:
        // the coverage gate asks every knob in the window whether it carries
        // one, and a Trigger listening to the slider inside would answer no.
        if (bound.knob != nullptr)
            paramMenu::attachTo (host, *bound.knob, bound.owner, spec);
        else
            paramMenuTriggers.push_back (std::make_unique<paramMenu::Trigger> (
                *bound.slider, host->contextFor (bound.owner, spec)));
    }
}

void InstrumentPanel::bindRotary (juce::Slider& slider, DewKnob* knob,
                                  std::function<juce::ValueTree()> owner,
                                  const juce::Identifier& property,
                                  const juce::String& transactionName)
{
    const auto& spec = requireInstrumentParamSpec (property);

    // One transaction per gesture, so dragging a knob is a single undo step
    // rather than several hundred. The protocol lives in RotaryGesture now;
    // this function is what is left of it, which is the write.
    gesture.attach (slider,
                    [this, &slider, &spec, owner, property, transactionName] (bool continuing)
                    {
                        if (updating)
                            return false;

                        auto tree = owner();

                        if (! tree.isValid())
                            return false;

                        // Integer-valued properties must stay integers in the file:
                        // writing a double would change the JSON from `0` to `0.0`
                        // and, worse, make the schema's type coercion do the
                        // rounding instead of this code.
                        const juce::var value = spec.integral ? juce::var ((int) slider.getValue())
                                                              : juce::var (slider.getValue());

                        ProjectEdits::setProperty (tree, property, value,
                                                   &document.getUndoManager(), transactionName,
                                                   continuing);
                        return true;
                    });

    boundRotaries.push_back ({ &slider, knob, owner, property });
}

void InstrumentPanel::attachKnob (DewKnob& knob, std::function<juce::ValueTree()> owner,
                                  const juce::Identifier& property,
                                  const juce::String& transactionName)
{
    // The caption, the range, the step, the decimals, whether it is bipolar and
    // whether it sweeps logarithmically all came from the ParamSpec when the
    // knob was constructed. Only the WRITE is left, and it is the same write a
    // stepper does - hence one bindRotary under both.
    bindRotary (knob.getSlider(), &knob, std::move (owner), property, transactionName);
    addAndMakeVisible (knob);
}

juce::ValueTree InstrumentPanel::selectedChannel() const
{
    return ProjectEdits::findChannel (document.getState(), editorState.getSelectedChannelId());
}

void InstrumentPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void InstrumentPanel::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&)
{
    // Not ids::OSC: the oscillator section listens for its own nodes, by
    // identity rather than by type - this filter would fire for every channel's.
    if (tree.hasType (ids::CHANNEL) || tree.hasType (ids::AMP) || tree.hasType (ids::MIXER_TRACK))
        refresh();
}

void InstrumentPanel::refresh()
{
    const juce::ScopedValueSetter<bool> quiet (updating, true);

    const auto channel = selectedChannel();
    const auto valid = channel.isValid();

    setEnabled (valid);

    // The one place the panel decides which face it is showing. resized() reads
    // the cached answer rather than asking the document again, so the layout
    // and the visibility can never disagree.
    showingAny = valid;
    showing = valid ? ProjectEdits::instrumentTypeOf (channel).value_or (InstrumentType::synth)
                    : InstrumentType::synth;

    const auto faceFor = [this, &channel] (InstrumentType type, const juce::Identifier& node)
    { return showingAny && showing == type ? channel.getChildWithName (node) : juce::ValueTree(); };

    oscSection.setOwner (faceFor (InstrumentType::synth, ids::INSTRUMENT));
    sampleSection.setOwner (faceFor (InstrumentType::audio, ids::SAMPLE));
    soundFontSection.setOwner (faceFor (InstrumentType::soundfont, ids::SOUNDFONT));

    oscSection.setVisible (showing == InstrumentType::synth);
    sampleSection.setVisible (showing == InstrumentType::audio);
    soundFontSection.setVisible (showing == InstrumentType::soundfont);

    // Base pitch is what a step written on the grid is pitched at, so every
    // channel that takes notes has one. The amplitude envelope belongs to the
    // oscillators alone: a soundfont region carries its own, and a second one
    // stacked on top would be two places holding the same fact.
    knobGroups.clear();

    if (showing == InstrumentType::synth)
        knobGroups.push_back ({ &attackKnob, &decayKnob, &sustainKnob, &releaseKnob });

    knobGroups.push_back ({ &volumeKnob, &panKnob });

    for (auto* c : { &attackKnob, &decayKnob, &sustainKnob, &releaseKnob })
        c->setVisible (showing == InstrumentType::synth);

    resized();

    chainHost.setOwner (channel,
                        channel.isValid() ? channel[ids::name].toString() : juce::String());

    if (! valid)
    {
        titleLabel.setText (tr (StringId::instrument_empty), juce::dontSendNotification);
        return;
    }

    titleLabel.setText (channel[ids::name].toString(), juce::dontSendNotification);

    const auto amp = channel.getChildWithName (ids::INSTRUMENT).getChildWithName (ids::AMP);

    attackKnob.setValue ((double) amp[ids::attack], juce::dontSendNotification);
    decayKnob.setValue ((double) amp[ids::decay], juce::dontSendNotification);
    sustainKnob.setValue ((double) amp[ids::sustain], juce::dontSendNotification);
    releaseKnob.setValue ((double) amp[ids::release], juce::dontSendNotification);

    volumeKnob.setValue ((double) channel[ids::volume], juce::dontSendNotification);
    panKnob.setValue ((double) channel[ids::pan], juce::dontSendNotification);
}

void InstrumentPanel::paint (juce::Graphics& g)
{
    g.fillAll (tokens::colour::surface);

    // No rule down the left edge any more. The seam between the editor and this
    // panel is one thing and it is drawn once, by the divider that is also what
    // you drag and what folds the panel - two rules eight pixels apart, with a
    // column of window background between them, was the whole of "two redundant
    // gaps".

    // The panel is a stack of full-bleed BANDS - the channel, its instrument,
    // its effects - separated by a rule that runs edge to edge. The effect band
    // draws its own top rule; this is the one above the instrument.
    g.setColour (tokens::colour::dividerStrong);
    g.drawHorizontalLine (instrumentBand.getY(), 0.0f, (float) getWidth());

    paint::sectionHeading (
        g, instrumentBand.withHeight (tokens::size::stripHeading).withTrimmedLeft (space::md),
        tr (StringId::instrument_heading));

    // The envelope against the levels, where the two share a knob row. Groups
    // that landed on rows of their own are already separated by the row break -
    // a rule as well would be saying it twice - which is why this list is
    // empty at every width the panel cannot fit both on one line.
    g.setColour (tokens::colour::divider);

    for (const auto& rule : knobRules)
        g.drawVerticalLine (rule.getCentreX(), (float) rule.getY(), (float) rule.getBottom());

    // The kind of instrument, beside its name. The panel already shows it in
    // which controls are on show, but only to someone who knows what a
    // soundfont section looks like.
    if (showingAny && ! titleGlyphBounds.isEmpty())
        icons::draw (g, glyph::forInstrument (showing),
                     titleGlyphBounds.toFloat().withSizeKeepingCentre (
                         (float) tokens::size::glyphMark, (float) tokens::size::glyphMark),
                     tokens::colour::textPrimary);
}

namespace
{

/** The presets for a channel, which kind it is decides - and, for a synth,
    which GENERATOR the slot on show is running.

    An unknown `source` offers nothing rather than guessing; buildSnapshot has
    already warned about it.

    A wavetable's patches are its own. Offered together, the only thing telling
    a wavetable preset from a classic one was the sound it made - the two are
    the same `"type": "synth"` in the file, and the picker had nothing else to
    go on.
*/
std::vector<Preset> presetsForChannel (const juce::ValueTree& channel, juce::StringRef generator)
{
    if (const auto type = instrumentTypeFor (channel[ids::source].toString()))
        return PresetLibrary::presetsFor (*type, generator);

    return {};
}

} // namespace

juce::String InstrumentPanel::generatorOnShow() const
{
    // Only a synth has generators. An audio or soundfont channel asks for all
    // of its own presets, which is what an empty generator means.
    return showing == InstrumentType::synth ? oscSection.selectedGeneratorId() : juce::String();
}

std::vector<PresetMenuRow> InstrumentPanel::presetMenuRowsFor() const
{
    // Grouped, because this is the picker the grouping was for: a synth channel
    // offers five sounds where an effect slot offers three, and the five fall
    // into bass, keys and pads without being forced.
    //
    // The description each row carries is no longer painted into it. It was
    // authored, serialised into every .dewpreset and shown to nobody for the
    // whole life of the feature, then shown to everybody at once - a picker
    // that says only "Pluck" makes you audition the list, and one that explains
    // every row at the same time is prose you have to read to find a name.
    return presetMenuRows (presetsForChannel (selectedChannel(), generatorOnShow()));
}

bool InstrumentPanel::applyPresetChoice (int choice)
{
    const auto channel = selectedChannel();
    const auto presets = presetsForChannel (channel, generatorOnShow());

    if (choice < 1 || choice > (int) presets.size())
        return false;

    return ProjectEdits::applyInstrumentPreset (channel, presets[(size_t) (choice - 1)],
                                                &document.getUndoManager());
}

void InstrumentPanel::showPresetMenu()
{
    const auto presets = presetsForChannel (selectedChannel(), generatorOnShow());

    if (presets.empty())
        return;

    // Built from the same rows the headless seam returns, not from a second
    // walk over the presets. The two had already drifted once: the seam was the
    // thing the tests read and showPresetMenu was the thing a person saw, and
    // only one of them had been taught to translate a name.
    juce::PopupMenu menu;
    addPresetRows (menu, presetMenuRowsFor(), onPresetHover);

    menu.setLookAndFeel (&getLookAndFeel());
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetButton),
                        [this] (int choice) { applyPresetChoice (choice); });
}

int InstrumentPanel::instrumentBandHeight() const
{
    const auto faceHeight = [this]
    {
        switch (showing)
        {
            case InstrumentType::synth: return oscSection.getRequiredHeight();
            case InstrumentType::audio: return SampleSection::requiredHeight;
            case InstrumentType::soundfont: return SoundFontSection::requiredHeight;
        }

        return 0;
    }();

    // The heading, then the face, the routing row and the knob grid. Each row
    // is followed by the gap `row` leaves behind, and the last of those gaps is
    // the band's own bottom padding, which is why nothing is added for it.
    //
    // The grid answers for its own depth rather than this counting knob rows:
    // six knobs are one row in a wide panel and two in a narrow one, and a
    // budget that assumed either would be wrong at the other.
    // Folded, the band IS its heading. Nothing below it is laid out, so nothing
    // below it may be budgeted for either - getRequiredHeight and resized read
    // this same answer, which is what keeps the panel and the sidebar's
    // scrollbar agreeing about how tall it is.
    if (! isInstrumentExpanded())
        return size::stripHeading;

    const auto rows = faceHeight + size::knob + KnobGrid::heightFor (knobPlan()) + 3 * space::sm;

    return size::stripHeading + rows;
}

int InstrumentPanel::knobBudgetWidth() const
{
    return juce::jmax (1, getWidth() - 2 * space::md - size::scrollThickness);
}

KnobGrid::Plan InstrumentPanel::knobPlan() const
{
    std::vector<int> sizes;

    for (const auto& group : knobGroups)
        sizes.push_back ((int) group.size());

    return KnobGrid::planForWidth (knobBudgetWidth(), sizes);
}

int InstrumentPanel::getRequiredHeight() const
{
    // The three bands, each asked for its own height rather than restated here.
    // resized() removes exactly these, in this order, so the two cannot drift
    // without the panel visibly disagreeing with its own scrollbar.
    return titleBandHeight + instrumentBandHeight() + chainHost.getPreferredHeight();
}

bool InstrumentPanel::isInstrumentExpanded() const
{
    return editorState.isInstrumentExpanded();
}

juce::PopupMenu InstrumentPanel::buildMenu() const
{
    juce::PopupMenu menu;

    const auto channel = selectedChannel();

    if (! channel.isValid())
        return menu;

    // What this channel PLAYS. Ticked rather than only offered, because the
    // panel's own glyph is the other place the answer is shown and a menu that
    // does not say which one you are on is a menu you have to guess in.
    juce::PopupMenu kinds;

    for (const auto& descriptor : instrumentDescriptors())
    {
        const auto isCurrent = ProjectEdits::instrumentTypeOf (channel) == descriptor.type;

        addGlyphItem (kinds, (int) MenuItem::instrumentBase + (int) descriptor.type,
                      tr (descriptor.displayName), glyph::forInstrument (descriptor.type), true,
                      isCurrent);
    }

    addGlyphSubMenu (menu, tr (StringId::instrument_change_label), std::move (kinds),
                     glyph::Action::open);

    menu.addSeparator();
    addGlyphItem (menu, (int) MenuItem::preset, tr (StringId::instrument_preset_help),
                  glyph::Action::preset);

    return menu;
}

void InstrumentPanel::applyMenuChoice (int choice)
{
    if (choice == (int) MenuItem::preset)
    {
        showPresetMenu();
        return;
    }

    const auto kind = choice - (int) MenuItem::instrumentBase;

    if (kind < 0 || kind >= kNumInstrumentTypes)
        return;

    ProjectEdits::setInstrumentType (selectedChannel(), (InstrumentType) kind,
                                     &document.getUndoManager());
}

void InstrumentPanel::showMenu (const juce::MouseEvent& event)
{
    // The title band's menu, and only the title band's. The sections below it
    // carry parameter menus of their own, and a press on one of those arrives
    // here as well as there.
    if (event.getEventRelativeTo (this).getPosition().y >= titleBandHeight)
        return;

    auto menu = buildMenu();

    if (menu.getNumItems() == 0)
        return;

    showMenuAt<InstrumentPanel> (menu, *this, event, [] (InstrumentPanel& panel, int choice)
                                 { panel.applyMenuChoice (choice); });
}

void InstrumentPanel::mouseDown (const juce::MouseEvent& event)
{
    popupPress.down (event, [this, &event] { showMenu (event); });
}

void InstrumentPanel::mouseDrag (const juce::MouseEvent&)
{
    // Armed in mouseDown and disarmed in both other phases - a right press that
    // travels one pixel completes a click otherwise. See PopupPress.
    if (popupPress.dragging())
        return;
}

void InstrumentPanel::mouseUp (const juce::MouseEvent&)
{
    if (popupPress.releasing())
        return;
}

void InstrumentPanel::resized()
{
    auto area = getLocalBounds();

    auto titleRow = area.removeFromTop (titleBandHeight).reduced (space::md);

    // The disclosure chevron, on the LEADING edge - the same place and the same
    // glyph an effect card puts it, because this band folds for the same reason
    // and a person should not have to learn it twice.
    collapseButton.setBounds (titleRow.removeFromLeft (size::iconButton)
                                  .withSizeKeepingCentre (size::iconButton, size::iconButton));
    titleRow.removeFromLeft (space::sm);
    collapseButton.setIcon (isInstrumentExpanded() ? icons::chevronUp() : icons::chevronDown());

    // The button on the right of the title, at the icon button's own size - the
    // title takes whatever is left, which is what it did before there was
    // anything beside it, and now gets back the 52px the word "Preset" cost.
    presetButton.setBounds (titleRow.removeFromRight (size::iconButton)
                                .withSizeKeepingCentre (size::iconButton, size::iconButton));
    titleRow.removeFromRight (space::sm);

    // The glyph column is taken only when there is a channel to describe, so an
    // empty panel's placeholder is not indented past a picture of nothing.
    titleGlyphBounds = showingAny ? titleRow.removeFromLeft (size::glyphColumn)
                                  : juce::Rectangle<int>();

    if (showingAny)
        titleRow.removeFromLeft (space::sm);

    titleLabel.setBounds (titleRow);

    // The instrument band: its heading, then its rows, inset from the panel's
    // edges. The band itself is full-bleed - the rule above it and its ground
    // run edge to edge - so the inset is on the CONTENT, not on the region.
    instrumentBand = area.removeFromTop (instrumentBandHeight());

    // Folded: nothing below the heading is laid out, and nothing below it is
    // visible either. Hiding rather than leaving them at stale bounds is what
    // keeps the walks that check "every control has real bounds inside its
    // parent" honest about a band that is not on show.
    const auto open = isInstrumentExpanded();

    oscSection.setVisible (open && showing == InstrumentType::synth);
    sampleSection.setVisible (open && showing == InstrumentType::audio);
    soundFontSection.setVisible (open && showing == InstrumentType::soundfont);

    for (const auto& group : knobGroups)
        for (auto* knob : group)
            knob->setVisible (open);

    if (! open)
    {
        knobRules.clear();
        chainHost.setBounds (area.withHeight (chainHost.getPreferredHeight()));
        return;
    }

    auto band = instrumentBand.reduced (space::md, 0).withTrimmedTop (size::stripHeading);

    const auto row = [&band] (int height)
    {
        auto r = band.removeFromTop (height);
        band.removeFromTop (space::sm);
        return r;
    };

    switch (showing)
    {
        case InstrumentType::synth:
            oscSection.setBounds (row (oscSection.getRequiredHeight()));
            break;

        case InstrumentType::audio:
            sampleSection.setBounds (row (SampleSection::requiredHeight));
            break;

        case InstrumentType::soundfont:
            soundFontSection.setBounds (row (SoundFontSection::requiredHeight));
            break;
    }

    // The envelope and the levels, as one grid rather than as two rows that
    // divided the same width by four and by two and so drew the same control at
    // two sizes. One cell width across both, the groups spread across the band,
    // and a rule between them only where they share a row.
    const auto plan = knobPlan();
    const auto placed = KnobGrid::place (row (KnobGrid::heightFor (plan)), plan);

    auto cell = placed.cells.begin();

    for (const auto& group : knobGroups)
        for (auto* knob : group)
            if (cell != placed.cells.end())
                knob->setBounds (*cell++);

    knobRules = placed.rules;

    // Exactly what it asked for, not "whatever is left". The band ends where
    // its cards end, and when they need more than the window has,
    // getRequiredHeight has already told MainComponent to scroll us.
    chainHost.setBounds (area.withHeight (chainHost.getPreferredHeight()));
}

} // namespace dew
