#pragma once

#include <vector>

#include "i18n/Strings.h"
#include "model/Preset.h"
#include "model/PresetCategory.h"

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
        /** The stable id, and the only thing here that never changes.

            It is unique, kebab-case, gate-checked in both directions against
            the CMake embed list, and it is what a file is called on disk. A
            Preset has no other identity - there is no id field in the format -
            so this is what a translated name is looked up BY, exactly as
            ParamChoice::id is what a stored choice is looked up by.
        */
        const char* fileName; ///< "warm-pad.dewpreset"

        /** What a person reads. Not what the FILE holds: see buildFor. */
        StringId name;
        StringId description;

        /** Which group of its type's presets this one belongs in.

            Here rather than on the builder for the same reason the name is: a
            builder describes a SOUND, and a category is editorial. Required,
            not optional - a preset the factory ships with no home would be one
            that silently fell out of the grouping the moment the library grew.
        */
        PresetCategory category;

        /** The sound, and nothing else - no name, no description. */
        Preset (*build)();
    };

    static const std::vector<Entry>& presets();

    /** `entry`'s preset, complete, with its name and description in the
        REFERENCE locale.

        This is what writes the committed presets, and what the byte-for-byte test
        compares against, so the file keeps a real English name in it: the
        format stays self-describing and hand-editable, and a preset somebody
        exports from another build still says what it is.

        A person reading the picker sees PresetLibrary::displayName instead,
        which resolves the same entry in THEIR locale. The file's name is the
        fallback for a preset the factory does not know - one from a future
        version, or one edited by hand.
    */
    static Preset buildFor (const Entry&);
};

} // namespace dew
