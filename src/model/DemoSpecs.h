#pragma once

#include "model/ProjectSchema.h"

namespace dew::demo
{

/** The schema nodes the demo builders build from.

    One home for the `childSpecFor` walks, because the builders are split across
    three translation units and each of them needs some of these. A spec is
    named once here and looked up by path exactly once, rather than each file
    re-deriving "the clips under the tracks under the playlist" for itself.
*/

inline const NodeSpec& channelsSpec()
{
    return childSpecFor (projectSpec(), "channels");
}

inline const NodeSpec& patternsSpec()
{
    return childSpecFor (projectSpec(), "patterns");
}

inline const NodeSpec& playlistSpec()
{
    return childSpecFor (projectSpec(), "playlist");
}

inline const NodeSpec& mixerSpec()
{
    return childSpecFor (projectSpec(), "mixer");
}

inline const NodeSpec& automationSpec()
{
    return childSpecFor (projectSpec(), "automations");
}

inline const NodeSpec& tracksSpec()
{
    return childSpecFor (playlistSpec(), "tracks");
}

inline const NodeSpec& clipsSpec()
{
    return childSpecFor (tracksSpec(), "clips");
}

inline const NodeSpec& effectsSpec()
{
    return childSpecFor (channelsSpec(), "effects");
}

inline const NodeSpec& notesSpec()
{
    return childSpecFor (patternsSpec(), "notes");
}

inline const NodeSpec& pointsSpec()
{
    return childSpecFor (automationSpec(), "points");
}

} // namespace dew::demo
