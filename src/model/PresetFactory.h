#pragma once

#include <vector>

#include "model/Preset.h"

namespace dew
{

/** The factory presets, built from code and shipped as files.

    The same split ProjectFactory and DemoLibrary use, and for the same reason:
    the TABLE lives here so a test can compare what is embedded against what is
    declared, and the LIBRARY reads the bytes that were actually committed, so
    what the picker offers is the file a reader can open and diff rather than a
    table only the program can see.

    Regenerate the files with `dew_render --write-presets presets`.
*/
struct PresetFactory
{
    struct Entry
    {
        const char* fileName; ///< "warm-pad.dewpreset"
        Preset (*build)();
    };

    static const std::vector<Entry>& presets();
};

} // namespace dew
