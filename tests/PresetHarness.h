#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include "model/Ids.h"
#include "model/Preset.h"

/** The two things all three preset test files reach for.

    firstChannel because a preset is applied to one and every group then asks
    what happened to it; instrumentPreset and subBassPreset because a preset
    written by hand is how the refusal cases are stated.
*/
namespace dew::testing
{

using namespace dew;

inline juce::ValueTree firstChannel (const juce::ValueTree& project)
{
    for (const auto& child : project)
        if (child.hasType (ids::CHANNEL))
            return child;

    return {};
}

inline Preset instrumentPreset (const juce::String& typeId, const juce::var& state)
{
    return { "instrument", typeId, "Test", "", {}, state, {} };
}

/** A synth preset that turns slot 0 into a sine and switches 1 and 2 off. */
inline Preset subBassPreset()
{
    auto* osc0 = new juce::DynamicObject();
    osc0->setProperty (ids::enabled, true);
    osc0->setProperty (ids::wave, "sine");
    osc0->setProperty (ids::octave, -1);
    osc0->setProperty (ids::gain, 0.9);

    auto* off = new juce::DynamicObject();
    off->setProperty (ids::enabled, false);

    juce::Array<juce::var> slots;
    slots.add (juce::var (osc0));
    slots.add (juce::var (off));
    slots.add (juce::var (new juce::DynamicObject (*off)));

    auto* amp = new juce::DynamicObject();
    amp->setProperty (ids::attack, 0.004);
    amp->setProperty (ids::sustain, 0.85);

    auto* state = new juce::DynamicObject();
    state->setProperty ("oscillators", slots);
    state->setProperty ("amp", juce::var (amp));

    return instrumentPreset ("synth", juce::var (state));
}

} // namespace dew::testing
