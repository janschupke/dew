#include "model/AssetPaths.h"

namespace dew
{

juce::File AssetPaths::sidecarFolderFor (const juce::File& projectFile)
{
    if (projectFile == juce::File())
        return {};

    return projectFile.getParentDirectory().getChildFile (projectFile.getFileNameWithoutExtension()
                                                          + " Assets");
}

juce::File AssetPaths::stagingFolder()
{
    auto folder = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                      .getChildFile ("dew")
                      .getChildFile ("Staging");

    folder.createDirectory();
    return folder;
}

juce::String AssetPaths::relativise (const juce::File& file, const juce::File& projectFile)
{
    if (file == juce::File())
        return {};

    if (projectFile == juce::File())
        return file.getFullPathName();

    const auto root = projectFile.getParentDirectory();

    // isAChildOf rather than comparing path prefixes: "/Music/Song Assets" is
    // not inside "/Music/Song" however much the strings suggest it is.
    if (! file.isAChildOf (root))
        return file.getFullPathName();

    return file.getRelativePathFrom (root);
}

juce::File AssetPaths::resolve (const juce::String& path, const juce::File& projectFile)
{
    if (path.isEmpty())
        return {};

    if (juce::File::isAbsolutePath (path))
        return juce::File (path);

    if (projectFile == juce::File())
        return {};

    return projectFile.getParentDirectory().getChildFile (path);
}

juce::File AssetPaths::defaultBrowseFolder()
{
    // In order of how much it looks like somewhere a person keeps music, and
    // the first one that is actually there wins. The working directory is last
    // because it is always a directory and never a good suggestion.
    for (const auto location : { juce::File::userMusicDirectory, juce::File::userDocumentsDirectory,
                                 juce::File::userHomeDirectory })
        if (const auto candidate = juce::File::getSpecialLocation (location);
            candidate.isDirectory())
            return candidate;

    return juce::File::getCurrentWorkingDirectory();
}

juce::File AssetPaths::nextTakeFile (const juce::File& folder, const juce::String& channelName)
{
    const auto stem = channelName.isNotEmpty() ? channelName : juce::String ("Audio");

    for (int take = 1; take < 10000; ++take)
    {
        auto candidate = folder.getChildFile (stem + " Take "
                                              + juce::String (take).paddedLeft ('0', 3) + ".wav");

        if (! candidate.existsAsFile())
            return candidate;
    }

    // Ten thousand takes of one channel in one folder is not a real situation,
    // but returning something that already exists would overwrite a recording.
    return folder.getChildFile (stem + " Take " + juce::Uuid().toDashedString() + ".wav");
}

} // namespace dew
