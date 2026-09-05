// =============================================================================
// DewKnob.
//
// The header's largest control, in its own translation unit. A knob is a value
// editor rather than a button: a needle to place, a caption and a readout to
// lay out, a compact mode for a row, a bipolar mode that fills from the centre,
// and two constructors - one taking a range, one taking the ParamSpec that
// already knows the range.
//
// Its mouseDown stays in DewControls.cpp with the other four. "A right-click
// never presses a button" is one rule about five controls, and keeping the five
// handlers under the one comment that states it is what makes that readable.
// =============================================================================

#include "ui/primitives/DewControls.h"

#include <cmath>

#include "model/ParamNames.h"
#include "model/ModuleCatalog.h"
#include "ui/design/Cursors.h"
#include "ui/design/DewLookAndFeel.h"
#include "ui/design/Gestures.h"
#include "ui/design/ParamPalette.h"
#include "ui/design/Tokens.h"

namespace dew
{

using namespace tokens;

// --- DewKnob -----------------------------------------------------------------

namespace
{

/** Suppresses the host LookAndFeel's rotary so DewKnob can draw its own. The
    slider stays for its behaviour - drag, wheel, keyboard, double-click reset -
    but contributes no pixels.
*/
class InvisibleRotaryLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float,
                           juce::Slider&) override
    {
    }
};

InvisibleRotaryLookAndFeel& invisibleRotary()
{
    static InvisibleRotaryLookAndFeel instance;
    return instance;
}

} // namespace

DewKnob::DewKnob (const juce::String& c, double minimum, double maximum, double interval)
    : caption (c)
{
    slider.setRange (minimum, maximum, interval);
    slider.setLookAndFeel (&invisibleRotary());

    // On the SLIDER, not on the knob: the slider fills the knob and is what the
    // pointer is actually over, and JUCE asks the deepest component for its
    // cursor rather than walking up to a parent.
    slider.setMouseCursor (cursor::value);

    // juce::Slider's constructor turns keyboard focus OFF, so every knob in dew
    // was unreachable by tab and Slider::keyPressed - the arrows, page up and
    // down, home and end - was dead code in every one of them. The rotary is
    // the control, so the rotary is what takes the focus.
    slider.setWantsKeyboardFocus (true);

    // Never called anywhere before this, so every knob in dew sat on JUCE's
    // default of 250 - which is not the same as having chosen 250.
    slider.setMouseDragSensitivity (gesture::dragPixelsForFullRange);
    slider.onValueChange = [this]
    {
        updateNeedle();

        if (onValueChange != nullptr)
            onValueChange();

        repaint();
    };

    slider.onDragStart = [this]
    {
        dragging = true;
        if (onEditStart != nullptr)
            onEditStart();
    };
    slider.onDragEnd = [this]
    {
        dragging = false;
        if (onEditEnd != nullptr)
            onEditEnd();
    };

    // The slider is what the pointer is actually over, so the knob listens to
    // it rather than to itself.
    slider.addMouseListener (this, false);

    addAndMakeVisible (slider);
}

DewKnob::DewKnob (const ParamSpec& spec)
    : DewKnob (tr (paramCaptionOf (*spec.property)), spec.minimum, spec.maximum, spec.interval)
{
    // The catalog's displayName, which has said "the automation picker,
    // tooltips" in its own comment since it was written and reached only the
    // first of those. A caption says what a knob is CALLED; the tooltip is what
    // the status bar shows the moment the pointer arrives, and a compact knob
    // has no caption at all - its own header says so and it had nothing to say
    // it with.
    setTooltip (tr (paramNameOf (*spec.property)));

    setNumDecimalPlaces (spec.decimals);
    setBipolar (spec.bipolar);

    // What this parameter DOES, taken from the catalog rather than chosen at
    // the call site. Here rather than in each panel because this constructor is
    // how every knob in dew is built: the instrument panel, the oscillator,
    // sample and soundfont sections and the effect cards all come through it,
    // so all of them are right without any of them saying anything.
    setFunctionColour (palette::forRole (roleOf (*spec.property)));

    // A logarithmic parameter gets a NormalisableRange, not a plain one: half a
    // millisecond to ten seconds is four and a half decades, and linearly every
    // usable value lives in the first one per cent of the travel.
    if (spec.curve == ParamCurve::logarithmic && spec.minimum > 0.0)
    {
        const auto skew = std::log (0.5)
                          / std::log ((std::sqrt (spec.minimum * spec.maximum) - spec.minimum)
                                      / (spec.maximum - spec.minimum));

        slider.setNormalisableRange ({ spec.minimum, spec.maximum, spec.interval, skew });
    }
}

void DewKnob::setValue (double v, juce::NotificationType notification)
{
    slider.setValue (v, notification);

    // Not only from onValueChange. The notification type says whether
    // LISTENERS hear about the change; the knob's own needle is not a listener,
    // and dontSendNotification - which every refresh() in dew uses - would
    // otherwise leave it pointing at the last value it was told about.
    updateNeedle();
    repaint();
}

void DewKnob::setNumDecimalPlaces (int places)
{
    decimalPlaces = juce::jmax (0, places);
    repaint();
}

void DewKnob::setFunctionColour (juce::Colour c)
{
    functionColour = c;
    repaint();
}

void DewKnob::setBipolar (bool shouldBeBipolar)
{
    bipolar = shouldBeBipolar;
    repaint();
}

void DewKnob::setCompact (bool shouldBeCompact)
{
    if (compact == shouldBeCompact)
        return;

    compact = shouldBeCompact;
    resized();
    repaint();
}

void DewKnob::setTooltip (const juce::String& text)
{
    juce::SettableTooltipClient::setTooltip (text);
    slider.setTooltip (text);

    // And as the slider's accessible NAME. The tooltip is already the one
    // sentence in the codebase that says what this control is - HoverHelp puts
    // it in the status bar and a gate refuses a control without one - so a
    // screen reader should be reading that same sentence rather than a second
    // vocabulary nobody keeps in step.
    slider.setTitle (text);
}

std::unique_ptr<juce::AccessibilityHandler> DewKnob::createAccessibilityHandler()
{
    return createIgnoredAccessibilityHandler (*this);
}

void DewKnob::resized()
{
    auto area = getLocalBounds();

    if (! compact)
    {
        area.removeFromTop (size::knobCaption);  // caption
        area.removeFromBottom (size::knobValue); // value
    }

    slider.setBounds (area);
}

float DewKnob::proportionOfValue() const
{
    const auto range = slider.getRange();
    const auto span = range.getLength();

    return span > 0.0 ? (float) ((slider.getValue() - range.getStart()) / span) : 0.0f;
}

void DewKnob::updateNeedle()
{
    const auto proportion = proportionOfValue();

    if (dragging || ! needleSeeded)
    {
        needle.snapTo (proportion);
        needleSeeded = true;
        return;
    }

    needle.animateTo (proportion, motion::valueMs, Ease::decelerate);
}

void DewKnob::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();

    // Seeded here as well as on the first change, because a knob can be laid
    // out and painted before anything ever writes to it - and the seed has to
    // be the value it HAS, not zero.
    if (! needleSeeded)
    {
        needle.snapTo (proportionOfValue());
        needleSeeded = true;
    }

    const auto proportion = needle.get();

    // A compact knob has room for the rotary and nothing else. Its caption
    // lives in the tooltip and its meaning in the fill: unipolar volume fills
    // from the left, bipolar pan from the centre.
    if (compact)
    {
        paint::rotary (g, area.toFloat(), proportion, isEnabled(), bipolar, functionColour);
        return;
    }

    g.setColour (colour::textSecondary);
    g.setFont (type::font (type::caption));
    g.drawText (caption, area.removeFromTop (size::knobCaption), juce::Justification::centred,
                false);

    auto valueArea = area.removeFromBottom (size::knobValue);

    paint::rotary (g, area.toFloat(), proportion, isEnabled(), bipolar, functionColour);

    g.setColour (isEnabled() ? colour::textPrimary : colour::textDisabled);
    g.setFont (type::font (type::caption));
    g.drawText (juce::String (slider.getValue(), decimalPlaces), valueArea,
                juce::Justification::centred, false);

    // Around the whole knob, caption and readout included, rather than around
    // the rotary alone: the focus is on the slider INSIDE this, and a ring that
    // hugged it would sit in the middle of the control rather than on its edge.
    paint::focusRing (g, *this, focus::ringVisibleFor (hasKeyboardFocus (true)));
}
} // namespace dew
