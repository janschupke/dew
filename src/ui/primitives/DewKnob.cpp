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

#include "ui/primitives/DewKnob.h"
#include "ui/primitives/DewPaint.h"

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
    // was unreachable by tab and its key handler was dead code in every one of
    // them. The rotary is the control, so the rotary is what takes the focus.
    //
    // That handler is DewSlider's now, not juce::Slider's, and this comment
    // used to say the base one covered "the arrows, page up and down, home and
    // end". It never did: in JUCE 9.0.1 it answers the four arrows and nothing
    // else, and it refuses every one of them the moment a modifier is down.
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
    suffix = juce::String (spec.suffix);

    // What this parameter DOES, taken from the catalog rather than chosen at
    // the call site. Here rather than in each panel because this constructor is
    // how every knob in dew is built: the instrument panel, the oscillator,
    // sample and soundfont sections and the effect cards all come through it,
    // so all of them are right without any of them saying anything.
    setFunctionColour (palette::forRole (roleOf (*spec.property)));

    // The SPEC's own mapping, rather than a skew computed from its endpoints.
    //
    // This used to hand JUCE a power law whose midpoint was the geometric mean
    // of the range, which agrees with ParamSpec::fromNormalised at exactly
    // three points - 0, a half, and 1 - and nowhere else. At a quarter of the
    // travel an attack knob read one millisecond where a curve drawn to the
    // same height played six. The header of ParamSpec::fromNormalised has said
    // "this is what a KNOB reads" since it was written, and no knob read it.
    //
    // By value: a NormalisableRange outlives this call, and a ParamSpec is a
    // handful of numbers and a pointer to an ids:: entry.
    juce::NormalisableRange<double> range { spec.minimum, spec.maximum,
                                            [spec] (double, double, double t)
                                            { return spec.fromNormalised (t); },
                                            [spec] (double, double, double v)
                                            { return spec.toNormalised (v); } };

    // Set after construction: the conversion-function constructor takes no
    // interval, and a knob that lost its declared step would grow digits the
    // parameter has not got.
    range.interval = spec.interval;

    slider.setNormalisableRange (range);
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

juce::Rectangle<int> DewKnob::typedEditBounds() const
{
    return getLocalBounds().removeFromBottom (size::controlHeight);
}

void DewKnob::mouseDoubleClick (const juce::MouseEvent& event)
{
    // A compact knob draws no readout, so there is nothing here to double-click
    // and nowhere to put the box. The right button never opens it either: that
    // press belongs to the parameter menu.
    if (compact || event.mods.isPopupMenu() || ! isEnabled())
        return;

    if (onEditStart != nullptr)
        onEditStart();

    // The value the READOUT is showing, at the decimals it is showing it to, so
    // re-typing what is already there changes nothing - and without the suffix,
    // which is drawn beside the number and would not parse back.
    typed.begin (typedEditBounds(), slider.getValue(), decimalPlaces, functionColour,
                 [this] (double newValue)
                 {
                     slider.setValue (newValue, juce::sendNotificationSync);

                     if (onEditEnd != nullptr)
                         onEditEnd();
                 });
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
    typed.setBounds (typedEditBounds());
}

float DewKnob::proportionOfValue() const
{
    // Through the slider's own range, which is what the DRAG moves along. It
    // used to divide by the raw span, discarding the curve - so on an envelope
    // knob the needle sat pinned at the bottom for nine tenths of the sweep and
    // then raced, while the value underneath it moved smoothly. Every rotary in
    // dew but this one already painted from a skew-aware position, because
    // juce::Slider computes it that way before handing it to a LookAndFeel.
    //
    // Through getNormalisableRange rather than valueToProportionOfLength, which
    // is virtual and not const - it is a hook for a subclass, and this is a
    // question about the range.
    return (float) slider.getNormalisableRange().convertTo0to1 (slider.getValue());
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
    // The box covers the readout and part of the rotary while it is up; drawing
    // the knob under it would show a needle through a text field.
    if (typed.isActive())
        return;

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

    // With the unit. The catalog has declared a suffix for every knob that has
    // one since it was written, and only DewNumberField ever drew it - so an
    // envelope knob showed a bare number and left "seconds or milliseconds?" to
    // whoever was reading it.
    g.setColour (isEnabled() ? colour::textPrimary : colour::textDisabled);
    g.setFont (type::font (type::caption));
    g.drawText (juce::String (slider.getValue(), decimalPlaces) + suffix, valueArea,
                juce::Justification::centred, false);

    // Around the whole knob, caption and readout included, rather than around
    // the rotary alone: the focus is on the slider INSIDE this, and a ring that
    // hugged it would sit in the middle of the control rather than on its edge.
    paint::focusRing (g, *this, focus::ringVisibleFor (hasKeyboardFocus (true)));
}
} // namespace dew
