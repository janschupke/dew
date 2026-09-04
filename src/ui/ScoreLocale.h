#pragma once

#include "i18n/Strings.h"
#include "lang/Messages.h"

namespace dew
{

/** The score language's name for the locale the application is running in.

    Two catalogues, two locale types, and this is the one place they meet.
    dew_i18n knows the tag - "en", "de-CH" - because that is what a person chose
    and what Settings stores; dew_lang knows an enum, because it has no
    juce::String to hold a tag in and no global to keep one in either. dew_ui is
    the lowest layer that can see both, which is why the bridge is here and not
    in one of them.

    Called at the point of use rather than cached. Setting a language is a
    restart in dew today, but a cached locale would be the thing that quietly
    survived the day it is not.
*/
inline lang::Locale scoreLocale()
{
    return lang::localeFor (activeLocale().toStdString());
}

} // namespace dew
