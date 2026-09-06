#include "model/ProjectSerializer.h"

#include "model/GeneratorCatalog.h"

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

/** v14 -> v15: a slot's generator parameters moved onto that generator's node.

    Runs on the parsed JSON for the reasons the v5 migration gives: the schema's
    unknown-key sweep would report the flat keys as ones it does not recognise
    and drop them - a warning on a file that is perfectly valid for the version
    it claims - and by the time there is a tree the generator nodes have already
    been filled with defaults, so there is nothing left to migrate into.

    BOTH halves are moved, not only the one the slot is running. A file whose
    slot is classic still carries whatever wavetable settings somebody dialled
    in before switching back, and dropping them here would make loading and
    saving a v14 file a way to lose them. What the new shape saves is space in
    files written from NOW on, where a slot the factory made carries defaults.
*/
/** v19 -> v20: a clip's position and length, from bars into steps.

    Read the METRE first, because a bar is worth stepsPerBeat * beatsPerBar
    steps and both are properties of the file being loaded rather than of this
    build. A file written before those existed takes the same defaults the
    schema gives them, which is what its own clips were laid out against.

    Both keys are removed after converting, so a v19 file that also happened to
    carry a stray `startStep` from somewhere is not read twice.
*/
void migrateClipsToSteps (juce::var& project)
{
    auto* root = project.getDynamicObject();

    if (root == nullptr)
        return;

    const auto readInt = [root] (const char* key, int fallback)
    {
        const auto value = root->getProperty (key);
        return value.isVoid() ? fallback : (int) value;
    };

    const auto stepsPerBar = juce::jmax (1, readInt ("stepsPerBeat", 4))
                             * juce::jmax (1, readInt ("beatsPerBar", 4));

    auto* playlist = root->getProperty ("playlist").getDynamicObject();

    if (playlist == nullptr)
        return;

    auto* tracks = playlist->getProperty ("tracks").getArray();

    if (tracks == nullptr)
        return;

    for (const auto& trackValue : *tracks)
    {
        auto* track = trackValue.getDynamicObject();

        if (track == nullptr)
            continue;

        auto* clips = track->getProperty ("clips").getArray();

        if (clips == nullptr)
            continue;

        for (const auto& clipValue : *clips)
        {
            auto* clip = clipValue.getDynamicObject();

            if (clip == nullptr)
                continue;

            const auto startBars = (int) clip->getProperty ("startBar");
            const auto lengthBars = juce::jmax (1, (int) clip->getProperty ("lengthBars"));

            clip->setProperty ("startStep", juce::jmax (0, startBars) * stepsPerBar);
            clip->setProperty ("lengthSteps", lengthBars * stepsPerBar);

            clip->removeProperty ("startBar");
            clip->removeProperty ("lengthBars");
        }
    }
}

void migrateGeneratorParamsToNodes (juce::var& project)
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

        if (instrument == nullptr)
            continue;

        auto* slots = instrument->getProperty ("oscillators").getArray();

        if (slots == nullptr)
            continue;

        for (const auto& slotValue : *slots)
        {
            auto* slot = slotValue.getDynamicObject();

            if (slot == nullptr)
                continue;

            // Which key belongs to which generator is the registry's answer,
            // not a list written out here - see GeneratorCatalog.h.
            for (const auto& generator : generatorDescriptors())
            {
                const juce::Identifier key (generator.id);

                // Already in the new shape: leave it alone. A file can arrive
                // here claiming an older version while carrying the current
                // one - a test builds exactly that, and so does anything that
                // edits a version field by hand - and overwriting the node it
                // already has would empty it.
                //
                // An OBJECT, not merely the key: the wavetable generator's node
                // and the wavetable CHOICE are both spelled "wavetable", so a
                // v14 slot has that key already and it holds a string. Asking
                // whether the key exists skipped the migration entirely and
                // left every wavetable setting behind.
                if (slot->getProperty (key).getDynamicObject() != nullptr)
                    continue;

                auto moved = juce::var (new juce::DynamicObject());

                for (int i = 0; i < generator.numParams; ++i)
                {
                    const auto& property = *generator.params[i].property;

                    if (! slot->hasProperty (property))
                        continue;

                    moved.getDynamicObject()->setProperty (property, slot->getProperty (property));
                    slot->removeProperty (property);
                }

                slot->setProperty (key, moved);
            }
        }
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

    if (version < 15)
        migrateGeneratorParamsToNodes (parsed);

    if (version < 20)
        migrateClipsToSteps (parsed);

    // v18's `lfo` node has no entry here on purpose. It was ADDED rather than
    // moved, every property in it has a declared default, and treeFromVar
    // materialises a missing non-array child from those - so a v17 slot loads
    // as one whose LFO is switched off, which is what it was.

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
