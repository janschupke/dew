// =============================================================================
// MixerStrip - everything it draws.
//
// One of two translation units behind ui/MixerStrip.h. Split because the strip
// had reached the four hundred code lines the tree allows a file, and at the
// seam that was already there: the menu, the layout and the tree listeners on
// one side, and the surface on the other.
//
// Three of the four painters here draw things the strip has no CHILD for - the
// meter, the name and the routing dots - which is the strip's whole shape: a
// column with a fader and a knob in it and the rest painted, because a child
// per mark would be a dozen components per strip in a mixer that holds
// thirty-two of them.
// =============================================================================

#include "ui/primitives/DewPaint.h"
#include "ui/MenuSeam.h"
#include "ui/MixerStrip.h"

#include <optional>
#include <utility>

#include "i18n/Strings.h"
#include "model/ProjectSchema.h"
#include "model/EntityColour.h"
#include "model/ProjectEdits.h"
#include "ui/ColourMenu.h"
#include "ui/RowSilence.h"
#include "ui/design/Glyphs.h"
#include "ui/design/MenuGlyph.h"
#include "ui/design/Cursors.h"
#include "ui/design/DewLookAndFeel.h"
#include "ui/design/Gestures.h"
#include "ui/design/ParamPalette.h"
#include "ui/primitives/DewMeter.h"

namespace dew
{

void MixerStrip::paint (juce::Graphics& g)
{
    const auto body = paint::bodyRect (*this, 2.0f);

    g.setColour (selected ? tokens::colour::surfaceRaised
                          : tokens::colour::surface.brighter (hover.lift()));
    g.fillRoundedRectangle (body, tokens::radius::md);

    // Master gets a neutral outline rather than an accent one: now that it
    // is selectable, an accent border on it always would read as selected.
    if (isMaster || selected)
    {
        g.setColour (selected ? tokens::colour::accent : tokens::colour::outline);
        g.drawRoundedRectangle (body, tokens::radius::md,
                                selected ? tokens::stroke::regular : tokens::stroke::hairline);
    }

    // A cap along the top edge, so which strip is selected is readable from
    // across the mixer rather than from a few percent of brightness.
    //
    // The same cap carries the strip's own colour when it has one and is not
    // selected. One band rather than two: selection is the louder fact and
    // has to win, and two stripes across a 60px strip is a pattern rather
    // than a signal.
    const auto cap = selected ? std::optional<juce::Colour> (tokens::colour::accent)
                              : entityColour::stored (track);

    if (cap.has_value())
    {
        g.setColour (*cap);
        g.fillRoundedRectangle (body.withHeight (3.0f), tokens::radius::xs);
    }

    paintMeter (g);
    paintName (g);
    paintRouting (g);

    // How many effects the strip carries, so it says what it holds without
    // having to be selected first. Top corner rather than the bottom, which
    // is where the fader's value box already is.
    const auto effectCount = ProjectEdits::countEffects (track);

    if (effectCount > 0 && ! badgeBounds.isEmpty())
    {
        g.setColour (tokens::colour::accent);
        g.fillRoundedRectangle (badgeBounds.toFloat(), tokens::radius::sm);

        g.setColour (tokens::colour::textOnAccent);
        g.setFont (tokens::type::font (tokens::type::caption, true));
        g.drawText (juce::String (effectCount), badgeBounds, juce::Justification::centred, false);
    }

    // Last, over the cap, the meter and the badge, the way a rack row and a
    // playlist header now do it.
    silence::paintOver (g, getLocalBounds(), (bool) track[ids::mute]);
}

void MixerStrip::paintMeter (juce::Graphics& g)
{
    using namespace tokens;

    if (meterBounds.isEmpty())
        return;

    const auto well = meterBounds.toFloat();

    g.setColour (colour::wellDeep);
    g.fillRoundedRectangle (well, radius::xs);

    if (level <= 0.0f)
        return;

    // Scaled the way a level is heard rather than by amplitude: linear, a
    // healthy mix sits in the bottom fifth of the meter and looks broken.
    const auto proportion = meter::proportionForGain (level);

    auto bar = well.withTop (well.getBottom() - proportion * well.getHeight());

    // funcLevel below the mark rather than success, and that is the whole of
    // what the function palette still says about a level. The controls that SET
    // one - this strip's fader, the rack's volume knob, an oscillator's gain -
    // took the app's own colour when level stopped being a function colour, and
    // a meter is the other half of that: not a control you hold but the signal
    // it passes, which is what funcLevel was named for. success stays what it
    // has always been, which is a verdict, and warning and danger stay the two
    // verdicts a meter is actually allowed to give.
    g.setColour (level >= 1.0f                       ? colour::danger
                 : proportion > meter::hotProportion ? colour::warning
                                                     : colour::funcLevel);
    g.fillRoundedRectangle (bar, radius::xs);

    // Where the meter stops being nominal, said by POSITION as well as by hue -
    // the bar's height carries the level, but the threshold it crosses was
    // carried by the colour change alone.
    const auto hotY = well.getBottom() - meter::hotProportion * well.getHeight();

    g.setColour (colour::dividerStrong);
    g.fillRect (well.getX(), hotY, well.getWidth(), stroke::hairline);
}

void MixerStrip::paintName (juce::Graphics& g)
{
    using namespace tokens;

    if (nameBounds.isEmpty() || nameLabel.isVisible())
        return;

    paint::verticalText (g, nameBounds, nameLabel.getText(),
                         selected ? colour::textPrimary : colour::textSecondary,
                         type::font (type::small, true));
}

void MixerStrip::paintRouting (juce::Graphics& g)
{
    using namespace tokens;

    if (routingBounds.isEmpty())
        return;

    // One dot per channel arriving here, in the channels' own colours, and no
    // names. This used to be four rows of eleven-point text - the smallest type
    // in the window - spending a quarter of the strip's height on something you
    // read once and then recognise by colour anyway. The names are in the
    // tooltip, and in the list a click here opens.
    if (routedColours.isEmpty())
    {
        // A dash rather than nothing at all: an empty row and a row that has
        // not been laid out yet look identical, and one of them is a bug.
        g.setColour (colour::textDisabled);
        g.fillRect (routingBounds.toFloat().withSizeKeepingCentre ((float) routingDotSize,
                                                                   stroke::hairline));
        return;
    }

    const auto span = juce::jmin (routingBounds.getWidth(),
                                  routedColours.size() * (routingDotSize + space::xxs)
                                      - space::xxs);

    auto row = routingBounds.withSizeKeepingCentre (span, routingDotSize);

    for (int i = 0; i < routedColours.size() && row.getWidth() >= routingDotSize; ++i)
    {
        g.setColour (routedColours[i]);
        g.fillEllipse (row.removeFromLeft (routingDotSize).toFloat());
        row.removeFromLeft (space::xxs);
    }
}

} // namespace dew
