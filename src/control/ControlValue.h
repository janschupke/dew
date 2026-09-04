#pragma once

#include <vector>

#include "control/ControlTypes.h"

namespace dew::control
{

/** Checks an argument object against the shape its operation declared.

    Returns an empty string when it fits, and otherwise ONE sentence naming the
    argument and what was wrong with it. One sentence rather than a list because
    the first fault is almost always the only real one, and a caller reading
    five complaints about the same missing object fixes it once anyway.

    A key the shape does not declare is refused, not ignored. A tool called with
    `velocty` should be told so: silently dropping it means the note comes out at
    the default velocity and nothing anywhere says why, which is the failure mode
    the project file's own reader refuses for the same reason.

    Recurses into `object` and `array` fields, and reports the path it was at -
    "notes[2].pitch" - because an entry in the middle of a batch of two hundred
    is not findable from "a pitch was wrong".
*/
juce::String validateArgs (const std::vector<ArgSpec>& shape, const juce::var& args);

// --- reading a validated argument ---------------------------------------------
// Every one of these assumes validateArgs has already passed, which is what lets
// them return a value rather than an optional. The dispatcher calls it before it
// calls a handler, so a handler cannot be reached with an argument of the wrong
// type - and that is why these say nothing about failure.

juce::String textArg (const juce::var& args, const juce::Identifier& name,
                      const juce::String& fallback = {});
int intArg (const juce::var& args, const juce::Identifier& name, int fallback = 0);
double numberArg (const juce::var& args, const juce::Identifier& name, double fallback = 0.0);
bool flagArg (const juce::var& args, const juce::Identifier& name, bool fallback = false);

/** The entries of a batch argument, or an empty array if it was absent.

    Never nullptr: an operation whose only argument is optional is called with
    no entries and answers "applied 0", which is a true answer a caller can act
    on. Returning a pointer would put that check in thirty handlers.
*/
const juce::Array<juce::var>& arrayArg (const juce::var& args, const juce::Identifier& name);

/** True when the object carries this key at all.

    The difference between "set the name to empty" and "leave the name alone",
    which every upsert in the table has to be able to tell. Without it an
    optional argument cannot be distinguished from its own default, and a tool
    that updates one field of a channel would silently blank the other five.
*/
bool hasArg (const juce::var& args, const juce::Identifier& name);

/** An empty array, for arrayArg's absent case and for anything else that needs
    to return one by reference. */
const juce::Array<juce::var>& emptyArray();

} // namespace dew::control
