#include "ui/design/SystemMotionPreference.h"

#include <juce_core/juce_core.h>

#if JUCE_MAC
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace dew
{

bool systemPrefersReducedMotion()
{
#if JUCE_MAC
    // CFPreferences rather than NSWorkspace's accessibilityDisplayShouldReduce-
    // Motion, which would make this the only Objective-C++ file in the tree and
    // put OBJCXX in the project's languages for one boolean. Same preference,
    // read the way a C API reads it.
    const auto value = CFPreferencesCopyAppValue (CFSTR ("ReduceMotionEnabled"),
                                                  CFSTR ("com.apple.Accessibility"));

    if (value == nullptr)
        return false;

    const auto reduce = CFGetTypeID (value) == CFBooleanGetTypeID()
                        && CFBooleanGetValue ((CFBooleanRef) value);

    CFRelease (value);
    return reduce;

#elif JUCE_WINDOWS
    BOOL animations = TRUE;

    if (! SystemParametersInfo (SPI_GETCLIENTAREAANIMATION, 0, &animations, 0))
        return false;

    return ! animations;

#else
    // No preference to ask for, rather than a preference that is off: a Linux
    // desktop keeps this in GSettings, which dew does not link.
    return false;
#endif
}

} // namespace dew
