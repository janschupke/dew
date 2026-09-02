#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <vector>

namespace dew
{

/** What an automation clip can be pointed at.

    Deliberately a curated list rather than an arbitrary property path. A path
    would automate anything, including a channel's name or a pattern's length,
    and would silently break the moment a property was renamed. This is a
    declared table: everything in it is a continuous quantity that means
    something to move over time, and the engine resolves each entry to a direct
    index so the audio thread does no lookup.
*/
enum class AutomationScope
{
    channel,        ///< a channel's own volume or pan
    channelOsc,     ///< a parameter of one oscillator slot on a channel
    channelEffect,  ///< a parameter of one slot in a channel's chain
    mixerTrack,     ///< a mixer track's gain or pan
    mixerEffect,    ///< a parameter of one slot in a mixer track's chain
    master          ///< the master gain
};

AutomationScope automationScopeFromString (const juce::String&);
juce::String automationScopeToString (AutomationScope);

/** One automatable parameter.

    `property` is the ValueTree identifier on the target node. `minimum` and
    `maximum` are the range an automation point's 0..1 value maps onto, so the
    point editor is uniform whatever it is driving.
*/
struct AutomationParamSpec
{
    const juce::Identifier* property;
    const char* displayName;
    double minimum;
    double maximum;

    /** True for pan-like parameters, which a point editor should centre. */
    bool bipolar;

    /** True for frequency-like quantities, which map exponentially.

        A cutoff swept linearly from 20Hz to 18kHz spends four fifths of its
        travel above 3kHz, where almost nothing audible happens, and the last
        fifth crossing the entire musical range. Mapping it as min*(max/min)^v
        makes the middle of a drawn curve the middle of what you hear.
    */
    bool logarithmic = false;
};

/** Maps a 0..1 automation value onto a parameter's own range. */
double mapAutomationValue (const AutomationParamSpec&, double normalised);

/** Parameters automatable on a channel itself. */
const std::vector<AutomationParamSpec>& channelParams();

/** Parameters automatable on a mixer track itself. */
const std::vector<AutomationParamSpec>& mixerTrackParams();

/** Parameters automatable on the master. */
const std::vector<AutomationParamSpec>& masterParams();

/** Parameters automatable on one oscillator slot.

    Only offered for a slot in wavetable mode - see availableAutomationTargets.
    A classic oscillator has nothing here that means anything to move over time,
    and offering position for one would be a control that silently did nothing.
*/
const std::vector<AutomationParamSpec>& oscParams();

/** Parameters automatable on an effect of this type. */
const std::vector<AutomationParamSpec>& effectParams (const juce::String& effectType);

/** The spec for one property within a scope, or nullptr if it is not
    automatable - which is how a clip pointing at a parameter that no longer
    applies (an effect slot changed type) is dropped rather than misapplied.
*/
const AutomationParamSpec* findParamSpec (AutomationScope, const juce::String& effectType,
                                          const juce::Identifier& property);

/** Every target a project currently offers, as the picker shows them. */
struct AutomationTarget
{
    AutomationScope scope = AutomationScope::channel;
    int targetId = 0;       ///< channel id or mixer track id; 0 for master
    int slot = -1;          ///< effect or oscillator slot index, -1 when the scope has none
    juce::Identifier property;
    juce::String displayName;   ///< "Kick > Filter > Cutoff"
    double minimum = 0.0;
    double maximum = 1.0;
    bool logarithmic = false;
};

std::vector<AutomationTarget> availableAutomationTargets (const juce::ValueTree& project);

} // namespace dew
