#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace dew
{

/** Builds project trees from scratch. */
struct ProjectFactory
{
    /** A blank project: four channels routed to four mixer inserts, one empty
        16-step pattern, four empty playlist tracks. What File > New produces.
    */
    static juce::ValueTree createDefault();

    /** A four-bar demo used by the tests, by CI, and shipped as examples/demo.dew.
        Deliberately audible: a kick pulse, an offbeat bass, and a lead line, so a
        render that produces silence is obviously wrong.
    */
    static juce::ValueTree createDemo();
};

} // namespace dew
