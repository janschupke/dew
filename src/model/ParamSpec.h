#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace dew
{

/** How a parameter's 0..1 position maps onto its own units. */
enum class ParamCurve
{
    linear,

    /** Frequency- and time-like quantities, which map exponentially.

        A cutoff swept linearly from 20Hz to 20kHz spends four fifths of its
        travel above 3kHz, where almost nothing audible happens, and the last
        fifth crossing the entire musical range. Mapping it as min*(max/min)^v
        makes the middle of a drawn curve, and the middle of a knob's travel,
        the middle of what you hear.
    */
    logarithmic
};

/** What a parameter looks like when a person edits it. */
enum class ParamControl
{
    knob,     ///< the 0..1 quantities a hand turns
    field,    ///< frequencies and times, where the number itself matters
    choice,   ///< one of a named set
    toggle,
    stepper   ///< an integer with buttons
};

struct ParamChoice
{
    const char* id;           ///< what goes in the file
    const char* displayName;
};

/** One parameter, declared once.

    Everything downstream is a VIEW of this: the schema's default, the engine's
    load clamp, the automation range and curve, and the range, interval,
    decimals and suffix of the control the user turns.

    Before this, a parameter was spelled out in as many as ten places. `cutoff`
    had three different maxima - 20000 in the engine's clamp, 18000 in the
    automation table and 18000 again in the UI's - two independent declarations
    of whether it was logarithmic, and two different display strings. Mixer gain
    reached 1.5 on the fader, 2.0 in the engine and 1.0 through automation, so a
    curve drawn to the top reached two thirds of the fader's travel. None of
    that was a bug anybody wrote; it is what four hand-maintained tables of the
    same numbers do over time.
*/
struct ParamSpec
{
    /** The ValueTree identifier on the target node. A pointer to an ids:: entry,
        never a string literal - renaming an identifier then cannot silently
        disconnect anything, because there is nothing to keep in step. */
    const juce::Identifier* property = nullptr;

    const char* displayName = "";   ///< "Cutoff" - the automation picker, tooltips
    const char* caption = "";       ///< "CUTOFF" - the control's own label
    const char* suffix = "";        ///< " Hz", or empty

    double minimum = 0.0;
    double maximum = 1.0;
    double defaultValue = 0.0;
    double interval = 0.01;         ///< editing step; 0 means continuous
    int decimals = 2;

    ParamCurve curve = ParamCurve::linear;
    ParamControl control = ParamControl::knob;

    bool bipolar = false;           ///< pan-like: a point editor centres it
    bool automatable = true;
    bool integral = false;          ///< stored as an int, like octave

    /** For ParamControl::choice: the values, and the default as text. */
    const ParamChoice* choices = nullptr;
    int numChoices = 0;
    const char* defaultText = nullptr;

    /** The default, in the var type the schema should store.

        The type matters: ProjectSchema drives its coercion off the runtime type
        of the default, so an int default and a double default are different
        declarations of the same number.
    */
    juce::var defaultVar() const;

    /** `value`, brought into range. The ONE clamp - the engine's load path and
        anything else that needs one calls this rather than restating the
        numbers. */
    float clamp (float value) const noexcept;
    double clamp (double value) const noexcept;

    /** 0..1 onto the parameter's own units, honouring the curve. */
    double fromNormalised (double normalised) const noexcept;

    /** The inverse, so a control can show where it sits. */
    double toNormalised (double value) const noexcept;
};

} // namespace dew
