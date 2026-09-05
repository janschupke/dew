#pragma once

#include <juce_core/juce_core.h>

namespace dew
{

/** Provenance of this binary: what version it is, and exactly which JUCE commit
    it was built against. The pin comes from cmake/DependencyPins.cmake via a
    compile definition, so the running app can always answer "which dependencies
    is this?" without consulting the build tree.
*/
struct BuildInfo
{
    /** What this application is called - the project() name, reaching the
        binary the way the version does. Deliberately NOT a catalogue key: a
        product name is not a sentence, and the one thing a translator must not
        do to it is translate it. */
    static juce::String name();

    static juce::String version();
    static juce::String jucePin();
    static juce::String summary();
};

} // namespace dew
