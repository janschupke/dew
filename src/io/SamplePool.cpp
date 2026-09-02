#include "io/SamplePool.h"

#include "model/AssetPaths.h"

namespace dew
{

SamplePool::SamplePool()
{
    formats.registerBasicFormats();
}

SamplePool::Key SamplePool::keyFor (const juce::File& file)
{
    return { file.getFullPathName(), file.getLastModificationTime().toMilliseconds() };
}

void SamplePool::setProjectFile (const juce::File& file)
{
    // Not a cache invalidation: entries are keyed on the absolute path they
    // were read from, so the same audio under a new project file is the same
    // entry. Only what a RELATIVE path resolves to changes.
    projectFile = file;
}

const SamplePool::Entry& SamplePool::loadReference (const juce::String& storedPath)
{
    if (storedPath.isEmpty())
        return invalid;

    return load (AssetPaths::resolve (storedPath, projectFile));
}

const SamplePool::Entry& SamplePool::load (const juce::File& file)
{
    if (file == juce::File() || ! file.existsAsFile())
        return invalid;

    const auto key = keyFor (file);
    const auto existing = entries.find (key);

    if (existing != entries.end())
        return existing->second;

    Entry entry;

    if (std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (file) })
    {
        const auto length = (int) juce::jmin ((juce::int64) std::numeric_limits<int>::max(),
                                              reader->lengthInSamples);

        if (length > 0 && reader->numChannels > 0)
        {
            auto audio = std::make_shared<juce::AudioBuffer<float>> ((int) reader->numChannels, length);

            // Reads as float whatever the file's bit depth is, which is what the
            // renderer wants and what makes the peaks comparable across formats.
            reader->read (audio.get(), 0, length, 0, true, true);

            entry.peaks = WaveformPeaks::compute (*audio);
            entry.sourceSampleRate = reader->sampleRate > 0.0 ? reader->sampleRate : kDefaultSampleRate;
            entry.audio = std::move (audio);
        }
    }

    // Cached even when the read failed: a project pointing at deleted audio
    // would otherwise retry the open on every single document change.
    return entries.emplace (key, std::move (entry)).first->second;
}

void SamplePool::forget (const juce::File& file)
{
    const auto path = file.getFullPathName();

    // Every modification time for the path, not just the current one - the
    // point of forgetting is usually that the file on disk has just changed.
    for (auto it = entries.begin(); it != entries.end();)
        it = it->first.path == path ? entries.erase (it) : std::next (it);
}

void SamplePool::clear()
{
    entries.clear();
}

} // namespace dew
