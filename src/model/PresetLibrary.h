#pragma once

#include <vector>

#include "model/ModuleCatalog.h"
#include "model/Preset.h"

namespace dew
{

/** The factory presets, as they are actually shipped.

    Reads them from the binary rather than rebuilding them from the factory, for
    the reason DemoLibrary does: what a picker offers is then exactly the file
    that was committed and that a reader can open and diff. A preset that only
    exists as code cannot drift from its file, but it also cannot be inspected
    or edited.
*/
struct PresetLibrary
{
    /** Every preset that is both declared and embedded, in the factory's order. */
    static const std::vector<Preset>& all();

    /** What a person reads for `preset`, in the ACTIVE locale.

        Looked up by the preset's id - the file it came from - which is the one
        stable thing a .dewpreset has. Falls back to the name stored in the file
        for a preset the factory does not know: one from a later version, or one
        somebody wrote by hand.

        Resolved at the point of display rather than cached into all(), because
        all() is built once and a locale is chosen once, and a cache that
        outlived a language would be the thing nobody tested.
    */
    static juce::String displayName (const Preset&);

    /** The same for the sentence under the name. */
    static juce::String describe (const Preset&);

    /** The embedded bytes of one preset, or an empty string if it is not there. */
    static juce::String jsonFor (const juce::String& fileName);

    /** The presets for one effect type. A reverb card offers only reverbs, so a
        mismatch is not merely refused by ProjectEdits - it is never offered. */
    static std::vector<Preset> presetsFor (EffectType);

    /** The presets for one kind of channel, so the audio face of the instrument
        panel shows the audio presets without asking. */
    static std::vector<Preset> presetsFor (InstrumentType);
};

} // namespace dew
