#include "lang/Messages.h"

namespace dew::lang
{

std::string msg (Msg id, Locale locale)
{
    return std::string (messageText (id, locale));
}

std::string msg (Msg id, const MsgArgs& arguments, Locale locale)
{
    return formatMessage (messageText (id, locale), arguments, localeTag (locale));
}

} // namespace dew::lang
