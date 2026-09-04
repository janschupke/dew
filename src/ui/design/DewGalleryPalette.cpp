// =============================================================================
// Every colour token, as a chip with its name under it.
//
// A token the gallery does not show is a token nobody knows they have - which
// is why the eight-entry channel ramp went unused while the application quietly
// carried a four-entry copy of it.
// =============================================================================

#include "ui/design/DewGalleryPalette.h"

#include <vector>

#include "ui/design/Tokens.h"

namespace dew::galleryPalette
{

using namespace tokens;

namespace
{

/** A swatch's column and its row pitch. heightFor() has to know how tall the
    grid comes out and paintSwatches() has to know where each chip goes. */
constexpr int swatchCell = 104;
constexpr int swatchRow = 48;

struct Swatch
{
    const char* name;
    juce::Colour value;
};

std::vector<Swatch> swatchList()
{
    return {
        { "wellDeep", colour::wellDeep },
        { "well", colour::well },
        { "background", colour::background },
        { "surface", colour::surface },
        { "surfaceRaised", colour::surfaceRaised },
        { "surfaceHover", colour::surfaceHover },
        { "divider", colour::divider },
        { "dividerStrong", colour::dividerStrong },
        { "outline", colour::outline },
        { "textPrimary", colour::textPrimary },
        { "textSecondary", colour::textSecondary },
        { "textDisabled", colour::textDisabled },
        { "accent", colour::accent },
        { "accentMuted", colour::accentMuted },
        { "playhead", colour::playhead },
        { "recording", colour::recording },
        { "success", colour::success },
        { "warning", colour::warning },
        { "danger", colour::danger },
        { "beatShade", colour::beatShade },
        { "barShade", colour::barShade },
        { "keyBlack", colour::keyBlack },
        { "keyWhite", colour::keyWhite },
        // Both were missing, and the ramp's absence was why nothing outside a
        // test referred to it - a token the gallery does not show is a token
        // nobody knows they have.
        { "textOnAccent", colour::textOnAccent },
        { "channel 0", colour::channelColour (0) },
        { "channel 1", colour::channelColour (1) },
        { "channel 2", colour::channelColour (2) },
        { "channel 3", colour::channelColour (3) },
        { "channel 4", colour::channelColour (4) },
        { "channel 5", colour::channelColour (5) },
        { "channel 6", colour::channelColour (6) },
        { "channel 7", colour::channelColour (7) },

        // The function palette. Shown next to the channel ramp deliberately:
        // the two are the page's own proof that identity is the louder of them,
        // which is the whole reason they can share a screen.
        { "funcLevel", colour::funcLevel },
        { "funcStereo", colour::funcStereo },
        { "funcTone", colour::funcTone },
        { "funcTime", colour::funcTime },
        { "funcSpace", colour::funcSpace },
        { "funcModulation", colour::funcModulation },
        { "funcPitch", colour::funcPitch },
    };
}

int rowsFor (int width)
{
    const auto perRow = juce::jmax (1, width / swatchCell);
    return ((int) swatchList().size() + perRow - 1) / perRow;
}

} // namespace

int heightFor (int width)
{
    return rowsFor (width) * swatchRow;
}

void paintSwatches (juce::Graphics& g, juce::Rectangle<int> bounds)
{
    const auto swatches = swatchList();
    const auto perRow = juce::jmax (1, bounds.getWidth() / swatchCell);

    for (int i = 0; i < (int) swatches.size(); ++i)
    {
        juce::Rectangle<int> cellBounds (bounds.getX() + (i % perRow) * swatchCell,
                                         bounds.getY() + (i / perRow) * swatchRow, swatchCell - 6,
                                         swatchRow - 4);

        // As tall as a control, because the chips sit on the same page as the
        // real ones and 26 is the rung the ladder already names for that. It
        // was a bare 26 here, which is the restatement the size gate exists to
        // catch - it only went unnoticed because it was inline.
        auto chip = cellBounds.removeFromTop (size::controlHeight);
        g.setColour (swatches[(size_t) i].value);
        g.fillRoundedRectangle (chip.toFloat(), radius::sm);
        g.setColour (colour::outline);
        g.drawRoundedRectangle (chip.toFloat(), radius::sm, stroke::hairline);

        g.setColour (colour::textSecondary);
        g.setFont (type::font (type::caption));
        g.drawText (swatches[(size_t) i].name, cellBounds, juce::Justification::centredTop, false);
    }
}

} // namespace dew::galleryPalette
