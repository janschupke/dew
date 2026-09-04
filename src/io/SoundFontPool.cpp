#include "io/SoundFontPool.h"

#include "model/AssetPaths.h"

namespace dew
{

void SoundFontPool::setProjectFile (const juce::File& file)
{
    projectFile = file;
}

SoundFontPool::Key SoundFontPool::keyFor (const juce::File& file)
{
    return { file.getFullPathName(), file.getLastModificationTime().toMilliseconds() };
}

const SoundFontPool::Entry& SoundFontPool::loadReference (const juce::String& storedPath)
{
    if (storedPath.isEmpty())
        return invalid;

    return load (AssetPaths::resolve (storedPath, projectFile));
}

const SoundFontPool::Entry& SoundFontPool::load (const juce::File& file)
{
    if (file == juce::File())
        return invalid;

    const auto key = keyFor (file);

    if (const auto found = entries.find (key); found != entries.end())
        return found->second;

    auto result = SoundFontFile::read (file);

    Entry entry;
    entry.warnings = result.warnings;

    if (result.font.isValid())
        entry.font = std::make_shared<const SoundFontData> (std::move (result.font));

    return entries.emplace (key, std::move (entry)).first->second;
}

void SoundFontPool::forget (const juce::File& file)
{
    entries.erase (keyFor (file));
}

void SoundFontPool::clear()
{
    entries.clear();
}

} // namespace dew
