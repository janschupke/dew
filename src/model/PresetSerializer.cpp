#include "model/PresetSerializer.h"

#include "model/ModuleCatalog.h"
#include "model/ModuleState.h"

namespace dew
{

namespace
{

// The envelope's own keys. Not parameter names, and deliberately spelled here
// rather than in Ids.h: they name this file format, not a node in the document.
constexpr const char* kFormat = "format";
constexpr const char* kVersion = "formatVersion";
constexpr const char* kKind = "kind";
constexpr const char* kType = "type";
constexpr const char* kName = "name";
constexpr const char* kDescription = "description";
constexpr const char* kState = "state";

/** Validates a preset's payload against the descriptor its type names.

    Returns false when the type is not one this build knows, which is refused
    rather than guessed for the reason effectTypeFor is an optional.
*/
bool validateInto (Preset& preset, juce::StringArray& warnings)
{
    if (preset.isEffect())
    {
        const auto type = effectTypeFor (preset.typeId);

        if (! type.has_value())
            return false;

        preset.state = validateState (effectDescriptor (*type), preset.state, warnings);
        return true;
    }

    if (preset.isInstrument())
    {
        const auto type = instrumentTypeFor (preset.typeId);

        if (! type.has_value())
            return false;

        preset.state = validateState (instrumentDescriptor (*type), preset.state, warnings);
        return true;
    }

    return false;
}

} // namespace

juce::String PresetSerializer::toJsonString (const Preset& preset)
{
    auto* object = new juce::DynamicObject();

    object->setProperty (kFormat, kPresetFormatTag);
    object->setProperty (kVersion, kPresetFormatVersion);
    object->setProperty (kKind, preset.kind);
    object->setProperty (kType, preset.typeId);
    object->setProperty (kName, preset.name);
    object->setProperty (kDescription, preset.description);
    object->setProperty (kState, preset.state);

    const auto options = juce::JSON::FormatOptions()
                             .withSpacing (juce::JSON::Spacing::multiLine)
                             .withIndentLevel (2)
                             .withMaxDecimalPlaces (6);

    return juce::JSON::toString (juce::var (object), options);
}

PresetSerializer::LoadResult PresetSerializer::fromJsonString (const juce::String& json)
{
    LoadResult out;

    juce::var parsed;

    if (juce::JSON::parse (json, parsed).failed() || parsed.getDynamicObject() == nullptr)
    {
        out.result = juce::Result::fail ("This file is not readable as JSON.");
        return out;
    }

    auto* object = parsed.getDynamicObject();

    if (object->getProperty (kFormat).toString() != kPresetFormatTag)
    {
        out.result = juce::Result::fail ("This is not a dew preset file.");
        return out;
    }

    const auto version = (int) object->getProperty (kVersion);

    if (version > kPresetFormatVersion)
    {
        out.result = juce::Result::fail ("This preset was written by a newer version of dew.");
        return out;
    }

    if (version < 1)
    {
        out.result = juce::Result::fail ("This preset does not say which version it is.");
        return out;
    }

    out.preset.kind = object->getProperty (kKind).toString();
    out.preset.typeId = object->getProperty (kType).toString();
    out.preset.name = object->getProperty (kName).toString();
    out.preset.description = object->getProperty (kDescription).toString();
    out.preset.state = object->getProperty (kState);

    if (! validateInto (out.preset, out.warnings))
    {
        out.result = juce::Result::fail ("This preset is for \"" + out.preset.typeId
                                         + "\", which this version of dew does not have.");
        return out;
    }

    return out;
}

juce::Result PresetSerializer::writeToFile (const Preset& preset, const juce::File& file)
{
    // Via a temporary and swap, and with the line endings normalised, for the
    // reasons ProjectSerializer::writeToFile gives: an interrupted write must
    // not leave a half-file where a preset used to be, and juce::JSON::toString
    // ends its lines with CRLF, which would commit a file that no longer
    // matches what the generator writes on the next machine.
    juce::TemporaryFile temp (file);

    if (auto stream = temp.getFile().createOutputStream())
    {
        if (! stream->writeText (toJsonString (preset), false, false, "\n"))
            return juce::Result::fail ("Could not write to " + file.getFullPathName());

        stream->flush();
        stream.reset();

        if (! temp.overwriteTargetFileWithTemporary())
            return juce::Result::fail ("Could not replace " + file.getFullPathName());

        return juce::Result::ok();
    }

    return juce::Result::fail ("Could not create " + file.getFullPathName());
}

PresetSerializer::LoadResult PresetSerializer::readFromFile (const juce::File& file)
{
    if (! file.existsAsFile())
    {
        LoadResult out;
        out.result = juce::Result::fail ("There is no file at " + file.getFullPathName());
        return out;
    }

    return fromJsonString (file.loadFileAsString());
}

} // namespace dew
