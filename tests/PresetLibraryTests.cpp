// The presets that ship: every one loads, and matches its committed file.
//
// Split out of PresetTests.cpp along the tags it already carried. The shared
// helpers are PresetHarness.h.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include <PresetData.h>

#include "model/Ids.h"
#include "model/ModuleCatalog.h"
#include "model/ModuleState.h"
#include "model/PresetFactory.h"
#include "model/PresetLibrary.h"
#include "model/PresetSerializer.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "model/ProjectSchema.h"
#include "ui/EditorState.h"
#include "ui/EffectChainComponent.h"
#include "ui/InstrumentPanel.h"

#include "PresetHarness.h"

using namespace dew;
using namespace dew::testing;
using Catch::Approx;

TEST_CASE ("every shipped preset loads clean and names a type dew has", "[preset][library]")
{
    const auto& entries = PresetFactory::presets();

    // A control, so the loop below cannot pass by being empty.
    REQUIRE (entries.size() >= 20);
    REQUIRE (PresetLibrary::all().size() == entries.size());

    for (const auto& entry : entries)
    {
        INFO ("preset: " << entry.fileName);

        const auto json = PresetLibrary::jsonFor (entry.fileName);
        REQUIRE (json.isNotEmpty());

        const auto loaded = PresetSerializer::fromJsonString (json);
        INFO ("result: " << loaded.result.getErrorMessage());
        REQUIRE (loaded.ok());

        // A shipped preset carries no surprises: an unknown key, a value out of
        // range or an unreadable choice all WARN, so an empty list is the
        // assertion that every one of them is exactly what its type declares.
        INFO ("warnings: " << loaded.warnings.joinIntoString (" | "));
        REQUIRE (loaded.warnings.isEmpty());

        REQUIRE (loaded.preset.name.isNotEmpty());
        REQUIRE (loaded.preset.description.isNotEmpty());

        if (loaded.preset.isEffect())
            REQUIRE (effectTypeFor (loaded.preset.typeId).has_value());
        else
            REQUIRE (instrumentTypeFor (loaded.preset.typeId).has_value());
    }
}

TEST_CASE ("every effect type ships at least one preset", "[preset][library]")
{
    // Otherwise a picker on some card would open onto nothing, which reads as
    // broken rather than as empty.
    for (const auto& descriptor : effectDescriptors())
    {
        INFO ("effect " << descriptor.id);
        CHECK (PresetLibrary::presetsFor (descriptor.type).size() >= 2);
    }

    for (const auto& descriptor : instrumentDescriptors())
    {
        INFO ("instrument " << descriptor.id);
        CHECK (PresetLibrary::presetsFor (descriptor.type).size() >= 2);
    }
}

TEST_CASE ("a type's presets are only its own", "[preset][library]")
{
    for (const auto& descriptor : effectDescriptors())
        for (const auto& preset : PresetLibrary::presetsFor (descriptor.type))
        {
            INFO ("preset " << preset.name << " offered for " << descriptor.id);
            CHECK (preset.isEffect());
            CHECK (preset.typeId == juce::String (descriptor.id));
        }
}

TEST_CASE ("the embedded presets match the committed files", "[preset][library]")
{
    // The files under presets/ are what a reader opens; the binary is what the
    // picker offers. If they drift, one of them is a lie.
    const juce::File presets (DEW_PRESETS_DIR);

    for (const auto& entry : PresetFactory::presets())
    {
        const auto file = presets.getChildFile (entry.fileName);
        INFO ("file: " << file.getFullPathName());

        REQUIRE (file.existsAsFile());
        REQUIRE (PresetLibrary::jsonFor (entry.fileName).trim() == file.loadFileAsString().trim());
    }
}

TEST_CASE ("the committed presets are byte for byte what the factory writes", "[preset][library]")
{
    // Regenerate with `dew_render --write-presets presets` when this fails.
    // Normalised the way writeToFile normalises - see the demo library's twin.
    const juce::File presets (DEW_PRESETS_DIR);

    for (const auto& entry : PresetFactory::presets())
    {
        const auto file = presets.getChildFile (entry.fileName);
        INFO ("file: " << file.getFullPathName());

        REQUIRE (file.existsAsFile());
        REQUIRE (PresetSerializer::toJsonString (entry.build()).replace ("\r\n", "\n").trim()
                 == file.loadFileAsString().replace ("\r\n", "\n").trim());
    }
}

TEST_CASE ("nothing is embedded that the factory does not declare", "[preset][library]")
{
    // Walked BOTH ways, because the CMake list and the factory table are two
    // hand-written lists of the same set. A file in one and not the other is
    // the failure this catches, by name.
    juce::StringArray declared;

    for (const auto& entry : PresetFactory::presets())
        declared.add (entry.fileName);

    juce::StringArray embedded;

    for (int i = 0; i < PresetData::namedResourceListSize; ++i)
        embedded.add (PresetData::originalFilenames[i]);

    for (const auto& name : declared)
    {
        INFO ("declared but not embedded: " << name);
        CHECK (embedded.contains (name));
    }

    for (const auto& name : embedded)
    {
        INFO ("embedded but not declared: " << name);
        CHECK (declared.contains (name));
    }
}

TEST_CASE ("a preset file written by a newer dew is refused, not half-read", "[preset][library]")
{
    auto json = PresetLibrary::jsonFor ("plate.dewpreset");
    REQUIRE (json.isNotEmpty());

    const auto newer = json.replace ("\"formatVersion\": 1", "\"formatVersion\": 99");
    REQUIRE (newer != json);

    const auto loaded = PresetSerializer::fromJsonString (newer);
    CHECK_FALSE (loaded.ok());
}

TEST_CASE ("a project file is not mistaken for a preset", "[preset][library]")
{
    // The envelope's whole job. Without the tag check a .dew would load as a
    // preset with no parameters rather than being refused.
    const auto loaded = PresetSerializer::fromJsonString (R"({"format": "dew-project"})");
    CHECK_FALSE (loaded.ok());
}

TEST_CASE ("every shipped preset actually applies to its own kind of node", "[preset][library]")
{
    auto project = ProjectFactory::createDefault();
    auto channel = firstChannel (project);
    auto audio = ProjectEdits::addAudioChannel (project, "Take", nullptr);
    auto soundfont = ProjectEdits::addSoundFontChannel (project, "Font", nullptr);

    for (const auto& preset : PresetLibrary::all())
    {
        INFO ("preset: " << preset.name);

        if (preset.isInstrument())
        {
            // A target of the preset's own kind: applyInstrumentPreset refuses
            // a mismatch, which is the point of it, so one shared target would
            // be testing the refusal rather than the preset.
            const auto target = preset.typeId == "audio"       ? audio
                                : preset.typeId == "soundfont" ? soundfont
                                                               : channel;
            CHECK (ProjectEdits::applyInstrumentPreset (target, preset, nullptr));
            continue;
        }

        const auto type = effectTypeFor (preset.typeId);
        REQUIRE (type.has_value());

        auto slot = ProjectEdits::addEffect (project, channel, preset.typeId, nullptr);
        REQUIRE (slot.isValid());

        CHECK (ProjectEdits::applyEffectPreset (slot, preset, nullptr));

        ProjectEdits::removeEffect (channel, slot, nullptr);
    }
}
