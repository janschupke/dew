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
    static juce::String version();
    static juce::String jucePin();
    static juce::String summary();
};

} // namespace dew
