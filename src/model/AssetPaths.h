#pragma once

#include <juce_core/juce_core.h>

namespace dew
{

/** Where a project's audio files live, and how a .dew refers to them.

    A .dew has always been one self-contained JSON file. Audio is the first
    thing it cannot hold, so it gains a sidecar folder beside it - "Song.dew"
    and "Song Assets/" - and stores paths RELATIVE to itself. That is what lets
    a project be copied to another machine, or moved to another folder, without
    every clip going silent.

    A recording made before the project has ever been saved has no .dew to be
    relative to, so it goes to a staging folder instead and is copied into the
    sidecar on the first save. See ProjectDocument::saveDocument.
*/
struct AssetPaths
{
    /** The folder beside `projectFile` that holds its audio. Not created. */
    static juce::File sidecarFolderFor (const juce::File& projectFile);

    /** Where recordings go while the project is untitled. Created on demand. */
    static juce::File stagingFolder();

    /** `file` as it should be stored in `projectFile`.

        Relative when the file is inside the project's own folder, absolute
        otherwise - an imported sample somewhere else on the disk is still a
        perfectly good reference, it just is not portable, and rewriting it as
        a chain of "../" would be portable to nothing.
    */
    static juce::String relativise (const juce::File& file, const juce::File& projectFile);

    /** The inverse. An empty path gives an invalid File rather than the
        project's own folder, so "no audio yet" cannot read as a directory.
    */
    static juce::File resolve (const juce::String& path, const juce::File& projectFile);

    /** A take file in `folder` that does not exist yet: "Audio Take 001.wav",
        then 002, and so on. Never returns a path that would overwrite.
    */
    static juce::File nextTakeFile (const juce::File& folder, const juce::String& channelName);
};

} // namespace dew
