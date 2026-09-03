#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace dew::testing
{

/** The small project the suite runs on: four bars, one pattern, eighteen notes.

    This IS the four-bar groove that ProjectFactory::createDemo() used to build,
    reproduced here rather than called there, and that separation is the point.

    `createDemo` is two things at once - the first demo anybody opens, and the
    fixture behind a hundred-odd assertions about renders, stems, MIDI export,
    metre and the mixer. Those two jobs pull in opposite directions: a demo
    wants an arrangement that goes somewhere, and a fixture wants to be the
    smallest audible thing that renders fast and whose note count nobody has to
    keep re-deriving. Held together, every improvement to the demo was a bill
    the suite paid twice, in edited expectations and in seconds per run.

    So the fixture moved here, where the tests own it, and the demo is free to
    grow. Keep this small: a test that wants a longer arrangement should build
    it, or open a demo, rather than making everyone else render more.
*/
juce::ValueTree fixtureProject();

} // namespace dew::testing
