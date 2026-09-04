#pragma once

namespace dew
{

/** Whether the operating system has been asked to reduce motion.

    JUCE has no API for this on any platform - it wraps dark mode portably and
    stops there - so this is dew's one piece of per-OS code. It is one function
    with a defined answer everywhere: the preference on macOS and Windows, and
    false where there is nothing to ask.

    Read once at startup and whenever the setting is consulted, not cached: the
    answer is a person's accessibility preference and they may change it while
    dew is open.
*/
bool systemPrefersReducedMotion();

} // namespace dew
