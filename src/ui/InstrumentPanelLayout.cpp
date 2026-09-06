// =============================================================================
// InstrumentPanel's geometry.
//
// The same class, a second translation unit, for the reason PlaylistView.cpp is
// one beside PlaylistPaint.cpp: how tall each band is, how the knobs fall out
// at the width they are budgeted for, and where everything lands are one
// subject, and they share nothing with the menus and the document binding above
// them except the members they read.
//
// The split was forced by the four-hundred-line gate when the instrument band
// learned to fold. It is the seam that was already there.
// =============================================================================

#include "ui/InstrumentPanel.h"

#include "model/ModuleCatalog.h"

namespace dew
{

using namespace tokens;

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
    //
    // Scaled by the fold rather than branched on it, so that agreement holds on
    // every frame of the animation and not just at its two ends.
    const auto rows = faceHeight + size::knob + KnobGrid::heightFor (knobPlan()) + 3 * space::sm;

    return size::stripHeading + juce::roundToInt ((double) rows * (1.0 - (double) fold.get()));
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

void InstrumentPanel::updateFold()
{
    const auto target = isInstrumentExpanded() ? 0.0f : 1.0f;

    if (foldStated)
    {
        fold.animateTo (target, tokens::motion::panelMs);
        return;
    }

    foldStated = true;
    fold.snapTo (target);
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
    // Still SHOWING, which is not the same as still expanded: the controls stay
    // on screen for the length of the fold and go once it has arrived, the way
    // MainComponent keeps its panel visible until the width reaches zero.
    // Hidden rather than left at stale bounds, which is what keeps the walks
    // that check "every control has real bounds inside its parent" honest.
    const auto open = fold.get() < 1.0f;

    oscSection.setVisible (open && showing == InstrumentType::synth);
    sampleSection.setVisible (open && showing == InstrumentType::audio);
    soundFontSection.setVisible (open && showing == InstrumentType::soundfont);

    for (const auto& group : knobGroups)
        for (auto* knob : group)
            knob->setVisible (open);

    // Faded as well as squeezed, and that is not decoration. The band's
    // contents are children of this panel rather than of a clipping container,
    // so the effect chain rising underneath them paints OVER them - which on
    // its own cuts a hard line through the middle of a knob row halfway
    // through the fold and reads as a rendering fault. Fading turns the same
    // frames into a wipe, which is what they are.
    const auto shown = 1.0f - fold.get();

    oscSection.setAlpha (shown);
    sampleSection.setAlpha (shown);
    soundFontSection.setAlpha (shown);

    for (const auto& group : knobGroups)
        for (auto* knob : group)
            knob->setAlpha (shown);

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
