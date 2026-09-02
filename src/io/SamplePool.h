#pragma once

#include <limits>
#include <map>
#include <memory>

#include <juce_audio_formats/juce_audio_formats.h>

#include "engine/SampleProvider.h"
#include "engine/WaveformPeaks.h"
#include "model/Constants.h"

namespace dew
{

/** Audio files, read once and kept.

    This cache is load-bearing rather than an optimisation. buildSnapshot runs
    on EVERY document change - a knob turn, a clip drag, a rename - and an audio
    channel's snapshot has to carry its samples. Reading the file each time
    would put disk I/O and a decode behind every edit.

    Entries are keyed on the absolute path and the file's modification time, so
    re-recording over a path picks the new audio up instead of serving the take
    that is no longer there.

    The buffers are handed out as shared_ptr<const>. Const because a snapshot on
    the audio thread reads them while the message thread may hand the same
    buffer to the next snapshot, and shared because that is what lets the pool
    drop an entry while a snapshot the audio thread is still rendering keeps the
    audio alive to the end of the block.
*/
class SamplePool : public SampleProvider
{
public:
    SamplePool();

    struct Entry
    {
        std::shared_ptr<const juce::AudioBuffer<float>> audio;
        double sourceSampleRate = kDefaultSampleRate;
        WaveformPeaks peaks;

        bool isValid() const noexcept  { return audio != nullptr && audio->getNumSamples() > 0; }
    };

    /** The document the stored paths are relative to.

        Path resolution lives here rather than in buildSnapshot because this is
        already the class whose job is turning a reference into audio, and
        because a snapshot is built from a ValueTree alone - the tree does not
        know where its own file is.
    */
    void setProjectFile (const juce::File&);

    const juce::File& getProjectFile() const noexcept  { return projectFile; }

    /** A path as stored in a SAMPLE node, resolved and loaded. */
    const Entry& loadReference (const juce::String& storedPath);

    /** Message thread. Reads the file if it has not been read, and returns the
        cached entry otherwise. An unreadable or missing file yields an entry
        whose isValid() is false, cached as such - a project pointing at audio
        that has been deleted must not re-attempt the read on every edit.
    */
    const Entry& load (const juce::File& file);

    /** Drops a cached entry, so the next load re-reads. */
    void forget (const juce::File& file);

    void clear();

    /** How many files are cached. For tests. */
    int size() const noexcept  { return (int) entries.size(); }

    /** SampleProvider. What the snapshot builder needs, and nothing else - the
        peaks stay behind loadReference, because they are for drawing. */
    std::shared_ptr<const juce::AudioBuffer<float>>
        audioFor (const juce::String& storedPath, double& sourceSampleRate) override
    {
        const auto& entry = loadReference (storedPath);

        if (! entry.isValid())
            return nullptr;

        sourceSampleRate = entry.sourceSampleRate;
        return entry.audio;
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

    juce::AudioFormatManager formats;
    std::map<Key, Entry> entries;
    juce::File projectFile;
    Entry invalid;
};

} // namespace dew
