#pragma once

#include <cstddef>

#include <juce_gui_basics/juce_gui_basics.h>

#include "i18n/Strings.h"
#include "model/ModuleCatalog.h"
#include "ui/design/MenuGlyph.h"
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

/** The choices a parameter DECLARES, straight from the catalog.

    These were three tables of {value, display} written out here - a third copy
    of the wave, mode and source vocabularies, with their labels in hard-coded
    English while the catalog's are StringIds. So the boxes offered untranslated
    words and a fourth generator would have meant remembering this file.

    The fallback comes from the spec too, so a call site no longer restates the
    schema's default as a string beside the property it is reading.
*/
inline const ParamSpec& choicesOf (const juce::Identifier& property)
{
    return requireInstrumentParamSpec (property);
}

inline int idFor (const ParamSpec& spec, const juce::String& name)
{
    for (int i = 0; i < spec.numChoices; ++i)
        if (name == spec.choices[i].id)
            return i + 1;

    for (int i = 0; i < spec.numChoices; ++i)
        if (spec.defaultText != nullptr && juce::String (spec.defaultText) == spec.choices[i].id)
            return i + 1;

    return 1;
}

inline void fill (juce::ComboBox& box, const ParamSpec& spec)
{
    for (int i = 0; i < spec.numChoices; ++i)
        box.addItem (tr (spec.choices[i].displayName), i + 1);
}

/** The same, with a picture on every row.

    `glyphFor` is handed the STORED name rather than the row's index, so a
    caller reuses the model's own reader - waveformFromString - instead of
    keeping a second table in step with this one by counting.
*/
template <typename GlyphFor>
void fill (juce::ComboBox& box, const ParamSpec& spec, GlyphFor glyphFor)
{
    for (int i = 0; i < spec.numChoices; ++i)
        addGlyphItem (box, i + 1, tr (spec.choices[i].displayName), glyphFor (spec.choices[i].id));
}

inline const char* valueOf (const ParamSpec& spec, int selectedId)
{
    return spec.choices[(size_t) juce::jlimit (0, spec.numChoices - 1, selectedId - 1)].id;
}

} // namespace oscillatorChoices

/** One entry in the segmented header.

    A juce::Button rather than a painted rectangle, so it gets hover and press
    for free and so it turns up in the findAll<juce::Button> sweeps the
    selection tests use.
*/
class OscillatorSection::SlotButton : public PopupSafeButton<juce::Button>
{
public:
    explicit SlotButton (int i)
        : PopupSafeButton<juce::Button> ("OSC " + juce::String (i + 1))
        , index (i)
    {
        setTooltip (tr (StringId::oscillator_slot_help, Args {}.with ("index", i + 1)));
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
