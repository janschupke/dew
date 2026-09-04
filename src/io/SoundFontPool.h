#pragma once

#include <map>
#include <memory>

#include "engine/SoundFontProvider.h"
#include "io/SoundFontFile.h"

namespace dew
{

/** Soundfonts, read once and kept.

    A straight mirror of SamplePool, and load-bearing for the same reason:
    buildSnapshot runs on EVERY document change - a knob turn, a clip drag, a
    rename - and a soundfont channel's snapshot has to carry its font. Reading
    the file each time would put a hundred megabytes of disk and a parse behind
    every edit.

    Entries are keyed on the absolute path and the file's modification time, so
    replacing a font in place picks up the new one.

    A font that cannot be read is cached AS unreadable. A project pointing at a
    soundfont that is not on this machine must not re-attempt the read on every
    edit, and a missing font is an ordinary situation rather than an error: a
    soundfont is a library you own, so a project copied to another machine may
    well arrive before its fonts do.
*/
class SoundFontPool : public SoundFontProvider
{
public:
    struct Entry
    {
        std::shared_ptr<const SoundFontData> font;
        juce::StringArray warnings;

        bool isValid() const noexcept
        {
            return font != nullptr && font->isValid();
        }
    };

    /** The document the stored paths are relative to. */
    void setProjectFile (const juce::File&);

    const juce::File& getProjectFile() const noexcept
    {
        return projectFile;
    }

    /** A path as stored in a SOUNDFONT node, resolved and loaded. */
    const Entry& loadReference (const juce::String& storedPath);

    const Entry& load (const juce::File&);

    void forget (const juce::File&);
    void clear();

    /** How many fonts are cached. For tests. */
    int size() const noexcept
    {
        return (int) entries.size();
    }

    std::shared_ptr<const SoundFontData> soundFontFor (const juce::String& storedPath) override
    {
        const auto& entry = loadReference (storedPath);
        return entry.isValid() ? entry.font : nullptr;
    }

private:
    struct Key
    {
        juce::String path;
        juce::int64 modificationTime = 0;

        bool operator< (const Key& other) const noexcept
        {
            if (path != other.path)
                return path < other.path;

            return modificationTime < other.modificationTime;
        }
    };

    static Key keyFor (const juce::File&);

    std::map<Key, Entry> entries;
    juce::File projectFile;
    Entry invalid;
};

} // namespace dew
