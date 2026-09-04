#pragma once

#include <cstddef>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/design/Tokens.h"

#include "ui/OscillatorSection.h"

namespace dew
{

using namespace tokens;

/** What the oscillator panel's two translation units both need.

    INTERNAL: only OscillatorSection.cpp and OscillatorFaces.cpp include it.

    The choice tables, because a combo box is built in one file and read in the
    other; and SlotButton, because the slot machinery constructs the three of
    them and the faces lay them out.
*/

namespace oscillatorChoices
{

/** A combo box's items: the string the document stores, and what the panel
    calls it. One table per box rather than two parallel ladders, so an item's
    id is its index in the table and nothing has to be kept in step by hand.
*/
struct NamedChoice
{
    const char* value;
    const char* display;
};

inline const NamedChoice waveChoices[] {
    { "sine", "Sine" }, { "saw", "Saw" }, { "square", "Square" }, { "triangle", "Triangle" }
};

inline const NamedChoice modeChoices[] { { "classic", "Classic" }, { "wavetable", "Wavetable" } };

// "LFO", not "Lfo": capitalising the stored name works for every other box here
// and would be wrong for exactly this one.
inline const NamedChoice sourceChoices[] { { "envelope", "Envelope" }, { "lfo", "LFO" } };

/** The 1-based combo id of a stored name, falling back to the schema default.

    `fallback` is a VALUE, not an index, so a caller names the default it wants
    the way the schema does rather than counting rows to find it.
*/
template <size_t N>
int idFor (const NamedChoice (&choices)[N], const juce::String& name, const char* fallback)
{
    for (size_t i = 0; i < N; ++i)
        if (name == choices[i].value)
            return (int) i + 1;

    for (size_t i = 0; i < N; ++i)
        if (juce::String (fallback) == choices[i].value)
            return (int) i + 1;

    return 1;
}

template <size_t N> void fill (juce::ComboBox& box, const NamedChoice (&choices)[N])
{
    for (size_t i = 0; i < N; ++i)
        box.addItem (choices[i].display, (int) i + 1);
}

template <size_t N> const char* valueOf (const NamedChoice (&choices)[N], int selectedId)
{
    return choices[(size_t) juce::jlimit (0, (int) N - 1, selectedId - 1)].value;
}

} // namespace oscillatorChoices

/** One entry in the segmented header.

    A juce::Button rather than a painted rectangle, so it gets hover and press
    for free and so it turns up in the findAll<juce::Button> sweeps the
    selection tests use.
*/
class OscillatorSection::SlotButton : public juce::Button
{
public:
    explicit SlotButton (int i)
        : juce::Button ("OSC " + juce::String (i + 1))
        , index (i)
    {
        setTooltip ("Edit oscillator " + juce::String (i + 1));
    }

    /** The same rule the dew primitives follow: juce::Button completes a click
        for whichever mouse button pressed it, and a right-click on a slot tab
        asked for nothing. */
    void mouseDown (const juce::MouseEvent& event) override
    {
        if (event.mods.isPopupMenu())
            return;

        juce::Button::mouseDown (event);
    }

    void setSelected (bool s)
    {
        if (std::exchange (selected, s) != s)
            repaint();
    }
    void setSlotEnabled (bool e)
    {
        if (std::exchange (slotEnabled, e) != e)
            repaint();
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool /*down*/) override
    {
        auto body = paint::bodyRect (*this);

        g.setColour (selected      ? colour::surfaceHover
                     : highlighted ? colour::surfaceRaised.brighter (emphasis::surfaceLift)
                                   : colour::surfaceRaised);
        g.fillRoundedRectangle (body, radius::sm);

        g.setColour (selected ? colour::accent : colour::outline);
        g.drawRoundedRectangle (body, radius::sm, selected ? stroke::regular : stroke::hairline);

        // A dot rather than a second word: three of these share the panel's
        // width, and "OSC 1 ON" at a size that still reads does not fit.
        //
        // FILLED or HOLLOW, not green or grey. The colour said it on its own,
        // which is the one thing a dot this small must not do - green against
        // grey is the axis most colour blindness runs along, and there is no
        // room here for the word that would otherwise carry it.
        const auto dot = body.removeFromLeft (12.0f).withSizeKeepingCentre (5.0f, 5.0f);
        g.setColour (slotEnabled ? colour::success : colour::textDisabled);

        if (slotEnabled)
            g.fillEllipse (dot);
        else
            g.drawEllipse (dot, stroke::hairline);

        g.setColour (slotEnabled ? colour::textPrimary : colour::textDisabled);
        g.setFont (type::font (type::caption, selected));
        g.drawText (getButtonText(), body.toNearestInt(), juce::Justification::centred, false);
    }

    const int index;

private:
    bool selected = false;
    bool slotEnabled = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SlotButton)
};

} // namespace dew
