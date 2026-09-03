#include "model/ProjectSerializer.h"

#include "model/ProjectSchema.h"

namespace dew
{

namespace
{

/** v5 -> v6: one oscillator per channel became a fixed array of slots.

    Runs on the parsed JSON rather than on the tree, for two reasons. The
    schema's unknown-key sweep would otherwise report the legacy "osc" as a key
    it does not recognise and drop it - a warning on a file that is perfectly
    valid for the version it claims. And by the time there is a tree, the slots
    have already been filled with defaults, so there is nothing left to migrate
    into.

    Only the first slot is written here. Leaving the array one element long lets
    the schema materialise the rest, so there is exactly one description of what
    an unused oscillator looks like.
*/
void migrateOscillatorsToArray (juce::var& project)
{
    auto* root = project.getDynamicObject();

    if (root == nullptr)
        return;

    auto* channels = root->getProperty ("channels").getArray();

    if (channels == nullptr)
        return;

    for (const auto& channelValue : *channels)
    {
        auto* channel = channelValue.getDynamicObject();

        if (channel == nullptr)
            continue;

        auto* instrument = channel->getProperty ("instrument").getDynamicObject();

        if (instrument == nullptr || ! instrument->hasProperty ("osc"))
            continue;

        // Moved whatever its type: a malformed oscillator becomes a malformed
        // element, which the schema then reports with a path, rather than
        // disappearing silently here.
        juce::Array<juce::var> slots;
        slots.add (instrument->getProperty ("osc"));

        instrument->removeProperty ("osc");
        instrument->setProperty ("oscillators", slots);
    }
}

} // namespace

juce::String ProjectSerializer::toJsonString (const juce::ValueTree& project)
{
    auto value = varFromTree (project, projectSpec());

    // The envelope is the serializer's business, not the schema's: it identifies
    // the file before anything tries to interpret its contents.
    if (auto* object = value.getDynamicObject())
    {
        object->setProperty ("format", kFormatTag);
        object->setProperty (ids::formatVersion, kFormatVersion);
    }

    const auto options = juce::JSON::FormatOptions()
                             .withSpacing (juce::JSON::Spacing::multiLine)
                             .withIndentLevel (2)
                             .withMaxDecimalPlaces (6);

    return juce::JSON::toString (value, options);
}

ProjectSerializer::LoadResult ProjectSerializer::fromJsonString (const juce::String& json)
{
    LoadResult loaded;

    juce::var parsed;
    const auto parseResult = juce::JSON::parse (json, parsed);

    if (parseResult.failed())
    {
        loaded.result = juce::Result::fail ("This is not a valid .dew file: "
                                            + parseResult.getErrorMessage());
        return loaded;
    }

    auto* object = parsed.getDynamicObject();

    if (object == nullptr)
    {
        loaded.result = juce::Result::fail ("This is not a valid .dew file: the contents are "
                                            "not a JSON object.");
        return loaded;
    }

    if (object->getProperty ("format").toString() != juce::String (kFormatTag))
    {
        loaded.result = juce::Result::fail ("This is not a dew project file.");
        return loaded;
    }

    const auto version = (int) object->getProperty (ids::formatVersion);

    if (version > kFormatVersion)
    {
        loaded.result = juce::Result::fail ("This project was saved by a newer version of dew "
                                            "(format "
                                            + juce::String (version)
                                            + ", this build "
                                              "reads up to "
                                            + juce::String (kFormatVersion) + ").");
        return loaded;
    }

    if (version < 1)
    {
        loaded.result = juce::Result::fail ("This project has an invalid format version ("
                                            + juce::String (version) + ").");
        return loaded;
    }

    // Older-but-supported versions land here. The schema's per-property defaults
    // already cover anything that was merely ADDED, so a migration is needed
    // only where the shape of a node changed.
    if (version < 6)
        migrateOscillatorsToArray (parsed);

    loaded.tree = treeFromVar (parsed, projectSpec(), loaded.warnings);

    // Anything the older file lacked has just been filled in from the schema's
    // defaults, so what is now in memory IS a current-version document. Stamp it
    // as one, or the tree and the file it would be saved to disagree.
    loaded.tree.setProperty (ids::formatVersion, kFormatVersion, nullptr);

    return loaded;
}

juce::Result ProjectSerializer::writeToFile (const juce::ValueTree& project, const juce::File& file)
{
    // Write via a temporary and swap, so an interrupted save cannot leave the
    // user with a half-written project where their work used to be.
    juce::TemporaryFile temp (file);

    if (auto stream = temp.getFile().createOutputStream())
    {
        const auto json = toJsonString (project);

        if (! stream->writeText (json, false, false, "\n"))
            return juce::Result::fail ("Could not write to " + file.getFullPathName());

        stream->flush();
        stream.reset();

        if (! temp.overwriteTargetFileWithTemporary())
            return juce::Result::fail ("Could not replace " + file.getFullPathName());

        return juce::Result::ok();
    }

    return juce::Result::fail ("Could not create " + file.getFullPathName());
}

ProjectSerializer::LoadResult ProjectSerializer::readFromFile (const juce::File& file)
{
    LoadResult loaded;

    if (! file.existsAsFile())
    {
        loaded.result = juce::Result::fail (file.getFullPathName() + " does not exist.");
        return loaded;
    }

    return fromJsonString (file.loadFileAsString());
}

} // namespace dew
