#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/WaveformPeaks.h"
#include "ui/design/Tokens.h"
#include "ui/primitives/DewButtons.h"

namespace dew
{

/* Everything the design system DRAWS, and the two mouse helpers that go with
   it. DewPaint.cpp was already the one implementation file; this is its header.

   DewButtons.h comes with it for DewLabel alone, which styleCaption takes.
*/

/** The word beside a control that is not a knob, as a Label.

    paint::caption is the same statement DRAWN, for a component that paints its
    own; this is for the case where the caption has to be laid out beside a
    juce::ComboBox rather than painted over it. The instrument panel had it as a
    file-local helper, and the two toolbars that wanted the same word next to
    the same kind of control would each have grown their own.
*/
void styleCaption (DewLabel&, const juce::String& text);

namespace paint
{
/** The rotary every knob in dew uses.
    @param proportion  0..1 position within the range
    @param bipolar     true for pan-like controls, where the arc fills out
                       from the centre instead of from the left
    @param value       the arc's colour: what this control DOES, from
                       palette::forRole. The track and the pointer are chrome
                       and stay as they are - a knob whose ring, needle and arc
                       were all one hue would be a coloured knob rather than a
                       knob that says something.
*/
void rotary (juce::Graphics&, juce::Rectangle<float>, float proportion, bool enabled, bool bipolar,
             juce::Colour value);

void surface (juce::Graphics&, juce::Rectangle<int>, juce::Colour);
void wellBackground (juce::Graphics&, juce::Rectangle<int>);

// container() - a rounded surface with a hairline edge, drawn UNDER a group of
// controls - is gone with its one caller. The effect chain host drew it around
// cards that already draw a rounded outlined body of their own, which is two
// levels of containment saying the same thing, and it needed a gap of window
// background all round to be seen at all. A region of the window is a BAND: its
// own ground, a rule where it begins, and no gap. Nothing else ever called it.

/** Fills the region beyond the content with a visibly inert texture, so an
    empty area reads as "nothing here" rather than as a broken control.

    For a region with nothing to continue into it - the panel below the last
    channel row. Where the grid DOES continue, use beyondEnd() instead.
*/
void inertArea (juce::Graphics&, juce::Rectangle<int>);

/** Marks the part of a timeline that is past the end of the material.

    Drawn OVER a grid that has already been painted across the full width,
    so the rows and bar lines keep going and the region still reads as
    out of bounds. Replacing the grid with a hatch, which is what this used
    to do, left a dead rectangle wherever the view was wider than the music.
*/
void beyondEnd (juce::Graphics&, juce::Rectangle<int>, float edgeX);

/** A control's caption - the word under a knob or beside a number field.
    The smallest thing in the system, and deliberately so.
*/
void caption (juce::Graphics&, juce::Rectangle<int>, const juce::String&,
              juce::Justification = juce::Justification::centredLeft);

/** A panel's heading. Distinct from caption(): "EFFECTS" is a heading and
    "CUTOFF" is a caption, and drawing both at the same size was why the
    effect chain's own title read as smaller than the things inside it.
*/
void sectionHeading (juce::Graphics&, juce::Rectangle<int>, const juce::String&,
                     juce::Justification = juce::Justification::centredLeft);

/** Every "there is nothing here yet" message.

    One function rather than five, because when each panel picked its own
    size and colour the app ended up with the same kind of message drawn at
    10, 11 and 13 point, in two different greys - and the 11pt textDisabled
    one was unreadable against the hatch behind it.
*/
void emptyState (juce::Graphics&, juce::Rectangle<int>, const juce::String&,
                 juce::Justification = juce::Justification::centred);

/** A component's own rectangle, inset half a pixel.

    Written out ten times, because a one-pixel edge drawn on a whole
    coordinate straddles two pixels and comes out two pixels wide and grey.
    The half is stroke::whisper - half of the hairline it is making room
    for - which is why that token exists.
*/
juce::Rectangle<float> bodyRect (const juce::Component&);

/** The same, inset further first - for a control that sits in a gutter.

    A mixer strip leaves 2px between itself and the next one, and reached for
    `reduced (2.0f)`, which lands its edges on whole coordinates: the outline
    around a selected strip was the one stroke in the application still
    straddling two pixels and coming out grey. The gutter and the half pixel are
    different questions and both have to be answered.
*/
juce::Rectangle<float> bodyRect (const juce::Component&, float inset);

/** A toolbar's background: the surface, the rule along the bottom, and a
    divider between each group of controls.

    The piano roll's and the playlist's paint() bodies were byte-identical. They
    are the two toolbars there are, and a third would have copied one of them.
*/
void toolbarStrip (juce::Graphics&, const juce::Component&, const juce::Array<int>& groupDividers);

/** What an INPUT looks like: a rounded body with a hairline edge.

    A number field, a search field, a dropdown and the text box inside a stepper
    are one family, and they were drawn four ways. The field was rounded at
    radius::sm in colour::outline; the search field square-cornered in
    colour::divider; the dropdown rounded at radius::md, so a dropdown beside a
    field on the same row was a different SHAPE; and a stepper's number was not
    painted by dew at all - it fell through to LookAndFeel_V2::drawLabel, which
    draws a square one-pixel drawRect, so the one input in the window sitting
    directly against two dew buttons was the one that did not match them.

    @param fill    the ground: surfaceRaised at rest, surfaceHover under the
                   pointer - the same pair every other control uses.
    @param border  outline at rest, and the control's function colour while it
                   is being dragged or typed into.
*/
void inputBox (juce::Graphics&, const juce::Component&, juce::Colour fill, juce::Colour border);

/** The ring that says a control has the keyboard.

    Drawn by the primitive itself rather than through
    LookAndFeel::createFocusOutlineForComponent, which puts the ring in its own
    overlay window: that needs a ComponentPeer, and every UI test here paints
    into an Image with no peer - so JUCE's mechanism would be invisible to the
    suite and to dew_shot, which is to say untestable in the two places this
    codebase actually looks at its own pixels.

    @param focused  whether the control holds the keyboard. Passed IN rather
                    than read from the component, because grabKeyboardFocus does
                    nothing without a ComponentPeer and this harness has none -
                    a helper that asked for itself could never be shown to draw.
                    Callers pass hasKeyboardFocus (true), which is also what a
                    DewKnob needs: its focus lives on the slider inside it.
*/
void focusRing (juce::Graphics&, const juce::Component&, bool focused);

/** The ring that says where the KEYBOARD is on a canvas that paints its
    contents.

    The same statement as a focus ring and deliberately the same colour: a
    control gets one around its edge, a canvas gets one around the cell the
    arrow keys are on. Drawn on an arbitrary rectangle rather than on a
    component, because the thing it marks is not one.

    Takes `shown` for the reason focusRing takes `focused` - a headless harness
    has no ComponentPeer, so a painter that asked the component whether it had
    the keyboard could never be shown to draw.
*/
void cursorOutline (juce::Graphics&, juce::Rectangle<float>, bool shown);

/** A sample's waveform: one column of pixels per column of pixels, each
    showing the extremes over the span it covers.

    Picking a single bin per column instead makes a waveform shimmer as the
    view resizes, which is why all three painters did it this way - and
    having written it three times they had drifted to insets of 2, 2 and 3,
    so the same audio was a pixel taller in the sequencer than in the
    playlist.

    @param y        the full vertical extent; the trace is inset within it
    @param span     where the whole file maps to horizontally, which may
                    reach outside the visible area
    @param painted  the columns actually to draw
    @param colourAt the colour for a column, so a trim handle can dim what
                    is outside it without a second loop
*/
void waveform (juce::Graphics&, juce::Range<float> y, juce::Range<float> span,
               juce::Range<float> painted, const WaveformPeaks&,
               const std::function<juce::Colour (float x)>& colourAt);
} // namespace paint
} // namespace dew
