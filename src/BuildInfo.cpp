#include "BuildInfo.h"

namespace dew
{

juce::String BuildInfo::version()
{
    return DEW_VERSION_STRING;
}

juce::String BuildInfo::jucePin()
{
    return DEW_JUCE_PIN;
}

juce::String BuildInfo::summary()
{
    return "dew " + version() + "  ·  JUCE " + juce::String (JUCE_MAJOR_VERSION)
         + "." + juce::String (JUCE_MINOR_VERSION) + "." + juce::String (JUCE_BUILDNUMBER)
         + " @ " + jucePin().substring (0, 12);
}

} // namespace dew
