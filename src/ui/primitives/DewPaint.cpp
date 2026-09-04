// =============================================================================
// The painting every dew control shares.
//
// namespace dew::paint, in its own translation unit rather than at the bottom
// of the controls file.
//
// A caption, a body rectangle, an inert wash, a beyond-the-end wash, an empty
// state. None of it belongs to any one control - the three editors call most of
// it directly - and it was in DewControls.cpp only because that is where the
// first caller happened to be.
// =============================================================================

#include "ui/primitives/DewControls.h"

#include <cmath>

#include "ui/design/Cursors.h"
#include "ui/design/DewLookAndFeel.h"
#include "ui/design/Gestures.h"
#include "ui/design/Tokens.h"

namespace dew
{

using namespace tokens;

// --- shared painting ---------------------------------------------------------

void styleCaption (DewLabel& label, const juce::String& text)
{
    label.setText (text, juce::dontSendNotification);
    label.setFont (type::font (type::caption));
    label.setTextColourToken (colour::textSecondary);
    label.setJustificationType (juce::Justification::centred);

    // A caption labels the control beside it; the pointer belongs to that
    // control, and a Label that ate the press would take its hover help with it.
    label.setInterceptsMouseClicks (false, false);
}

void forwardChildMouseEventsTo (juce::Component& parent)
{
    for (auto* child : parent.getChildren())
        child->addMouseListener (&parent, /* wantsEventsForAllNestedChildComponents */ true);
}

namespace paint
{

void rotary (juce::Graphics& g, juce::Rectangle<float> bounds, float proportion, bool enabled,
             bool bipolar, juce::Colour value)
{
    constexpr auto startAngle = juce::MathConstants<float>::pi * 1.2f;
    constexpr auto endAngle = juce::MathConstants<float>::pi * 2.8f;

    const auto square = bounds
                            .withSizeKeepingCentre (
                                juce::jmin (bounds.getWidth(), bounds.getHeight()),
                                juce::jmin (bounds.getWidth(), bounds.getHeight()))
                            .reduced (2.0f);

    const auto radius = square.getWidth() * 0.5f;
    const auto centre = square.getCentre();
    const auto thickness = juce::jmax (2.5f, radius * 0.26f);
    const auto arcRadius = radius - thickness * 0.5f;
    const auto angle = startAngle + juce::jlimit (0.0f, 1.0f, proportion) * (endAngle - startAngle);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle,
                         true);
    // The track has to read as a control even when the value arc is empty. It
    // used to be `well`, which on a `surface` panel is nearly invisible, so a
    // knob at the bottom of its range looked like a stray tick mark rather than
    // a knob - which is exactly how the ADSR knobs at their minimums looked.
    g.setColour (colour::outline.withAlpha (emphasis::dimmed));
    g.strokePath (track, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    // A bipolar control fills out from the centre, so "no pan" reads as no fill
    // rather than as half a ring.
    const auto fillFrom = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;

    if (! juce::approximatelyEqual (angle, fillFrom))
    {
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                           juce::jmin (fillFrom, angle), juce::jmax (fillFrom, angle), true);
        g.setColour (enabled ? value : colour::dividerStrong);
        g.strokePath (arc, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    }

    juce::Path pointer;
    pointer.startNewSubPath (centre.x + (radius - thickness * 1.9f) * std::sin (angle),
                             centre.y - (radius - thickness * 1.9f) * std::cos (angle));
    pointer.lineTo (centre.x + (radius - thickness * 0.2f) * std::sin (angle),
                    centre.y - (radius - thickness * 0.2f) * std::cos (angle));

    g.setColour (enabled ? colour::textPrimary : colour::textDisabled);
    g.strokePath (pointer, juce::PathStrokeType (juce::jmax (stroke::regular, thickness * 0.42f),
                                                 juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
}

void surface (juce::Graphics& g, juce::Rectangle<int> bounds, juce::Colour c)
{
    g.setColour (c);
    g.fillRect (bounds);
}

void container (juce::Graphics& g, juce::Rectangle<int> bounds)
{
    if (bounds.isEmpty())
        return;

    const auto body = bounds.toFloat().reduced (stroke::whisper);

    g.setColour (colour::surface);
    g.fillRoundedRectangle (body, radius::md);

    g.setColour (colour::outline);
    g.drawRoundedRectangle (body, radius::md, stroke::hairline);
}

void wellBackground (juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour (colour::well);
    g.fillRect (bounds);
}

void inertArea (juce::Graphics& g, juce::Rectangle<int> bounds)
{
    // Darker than the well, with a faint diagonal hatch. The point is that a
    // large empty region should read as "there is nothing here", not as a
    // control that has stopped responding - which is exactly how the old
    // channel rack's 88% of empty panel read.
    if (bounds.isEmpty())
        return;

    g.setColour (colour::wellDeep);
    g.fillRect (bounds);

    g.setColour (colour::divider.withAlpha (emphasis::hatch));

    const auto spacing = 12.0f;
    const auto span = (float) (bounds.getWidth() + bounds.getHeight());

    for (float offset = 0.0f; offset < span; offset += spacing)
    {
        const auto x = (float) bounds.getX() + offset;
        g.drawLine (x, (float) bounds.getY(), x - (float) bounds.getHeight(),
                    (float) bounds.getBottom(), 0.5f);
    }
}

void beyondEnd (juce::Graphics& g, juce::Rectangle<int> bounds, float edgeX)
{
    if (bounds.isEmpty())
        return;

    // A scrim rather than a fill: the grid underneath stays visible, so the
    // view reads as continuing rather than as ending in a void, while still
    // being obviously not part of the pattern.
    g.setColour (colour::wellDeep.withAlpha (emphasis::dimmed));
    g.fillRect (bounds);

    // The edge itself carries the meaning, so it is drawn at full strength.
    g.setColour (colour::dividerStrong);
    g.fillRect (juce::Rectangle<float> (edgeX - 1.0f, (float) bounds.getY(), 2.0f,
                                        (float) bounds.getHeight()));
}

void caption (juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& text,
              juce::Justification justification)
{
    g.setColour (colour::textSecondary);
    g.setFont (type::font (type::caption));
    g.drawText (text, bounds, justification, false);
}

void sectionHeading (juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& text,
                     juce::Justification justification)
{
    g.setColour (colour::textSecondary);
    g.setFont (type::font (type::small, true));
    g.drawText (text, bounds, justification, false);
}

void emptyState (juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& text,
                 juce::Justification justification)
{
    // textSecondary, not textDisabled. An empty state is the one thing on an
    // empty panel, so it is the opposite of de-emphasised - it is the only
    // instruction the user has.
    g.setColour (colour::textSecondary);
    g.setFont (type::font (type::body));

    // Ellipsised rather than hard-clipped. The step grid puts a whole
    // missing-file path through here, into a row one rung tall, and a message
    // cut mid-word reads as the whole message. The ellipsis at least says that
    // there was more of it.
    g.drawText (text, bounds, justification, true);
}

juce::Rectangle<float> bodyRect (const juce::Component& c)
{
    return c.getLocalBounds().toFloat().reduced (stroke::whisper);
}

void focusRing (juce::Graphics& g, const juce::Component& c, bool focused)
{
    if (! focused)
        return;

    g.setColour (colour::accent);
    g.drawRoundedRectangle (bodyRect (c), radius::sm, stroke::bold);
}

void waveform (juce::Graphics& g, juce::Range<float> y, juce::Range<float> span,
               juce::Range<float> painted, const WaveformPeaks& peaks,
               const std::function<juce::Colour (float x)>& colourAt)
{
    if (peaks.isEmpty() || span.getLength() <= 0.0f)
        return;

    const auto centre = y.getStart() + y.getLength() * 0.5f;
    const auto halfHeight = juce::jmax (1.0f, y.getLength() * 0.5f - (float) size::waveformInset);

    for (int x = (int) painted.getStart(); x < (int) painted.getEnd(); ++x)
    {
        const auto from = ((float) x - span.getStart()) / span.getLength();
        const auto to = ((float) (x + 1) - span.getStart()) / span.getLength();

        const auto bin = peaks.range (from, to);
        const auto top = centre - bin.maximum * halfHeight;
        const auto bottom = centre - bin.minimum * halfHeight;

        g.setColour (colourAt ((float) x));
        g.fillRect ((float) x, juce::jmin (top, bottom), 1.0f,
                    juce::jmax (1.0f, std::abs (bottom - top)));
    }
}

} // namespace paint
} // namespace dew
