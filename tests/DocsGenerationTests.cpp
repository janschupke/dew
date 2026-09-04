#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <juce_core/juce_core.h>

#include "DocsSchema.h"

using namespace dew;

namespace
{

juce::File generatedFile (const juce::String& name)
{
    return juce::File { juce::String (DEW_WEBSITE_GENERATED_DIR) }.getChildFile (name);
}

} // namespace

TEST_CASE ("the website's schema is what the emitter writes", "[docs][website][gate]")
{
    // The examples/*.dew pattern - the committed file held byte for byte
    // against what produces it. git diff --exit-code was the alternative and
    // was rejected: it would need check.sh to build the tool, run it, and WRITE
    // INTO the working tree before diffing, and a gate that mutates the tree it
    // is judging cannot be run on a tree with work in it.
    const auto file = generatedFile ("score-schema.json");

    INFO ("file: " << file.getFullPathName());
    INFO ("regenerate: cmake --build --preset ci --target dew_docs && "
          "./build/ci/tools/dew_docs schema website/src/generated/score-schema.json");

    REQUIRE (file.existsAsFile());

    // No trim(). CI compares the same file with cmp in a second process, and
    // two gates disagreeing about one file is worse than either alone.
    CHECK (file.loadFileAsString().toStdString() == docs::schemaJson());
}

TEST_CASE ("the schema emitter writes the same bytes twice", "[docs][website][gate]")
{
    // In process, so it proves less than the cmp CI runs in a second process -
    // two runs inside one process agree on the same rubbish, which is exactly
    // how the score compiler's nondeterminism hid for a whole phase. This one
    // is free, and it is what fires the day somebody puts a std::map in a walk.
    CHECK (docs::schemaJson() == docs::schemaJson());
}

TEST_CASE ("every value kind is emitted, named and distinct", "[docs][website][gate]")
{
    // identifierOf's switch has no default, so -Wswitch-enum makes a new
    // ValueKind a compile error there. Nothing makes it an error in allKinds(),
    // and a kind named but not listed is one the reference silently omits.
    // This is the half that catches that.
    const auto kinds = docs::allKinds();

    // Control case: a gate over a short list is not a gate.
    REQUIRE (kinds.size() > 25);

    juce::StringArray identifiers;

    for (const auto kind : kinds)
    {
        const juce::String identifier { docs::identifierOf (kind) };

        INFO ("value kind: " << identifier);
        CHECK (identifier != "unknown");
        CHECK (! identifiers.contains (identifier));

        identifiers.add (identifier);
    }

    // Every kind a key names is one of them. `channelRef` and `scope` are NOT
    // covered by this - they are reached through a block header rather than a
    // KeySpec - which is the whole reason allKinds() is written out rather than
    // collected by walking the keys.
    for (const auto& block : lang::schema())
        for (const auto& key : block.keys)
            CHECK (std::find (kinds.begin(), kinds.end(), key.kind) != kinds.end());
}

TEST_CASE ("every block and key in the schema reaches the json", "[docs][website][gate]")
{
    // The claim Schema.h's own comment makes - that a key cannot exist without
    // being documented - is only true if the emitter actually walks all of it.
    const auto json = juce::String (docs::schemaJson());

    auto keys = 0;

    for (const auto& block : lang::schema())
    {
        INFO ("block: " << lang::nameOf (block.kind));
        CHECK (json.contains ("\"kind\": \"" + juce::String (lang::nameOf (block.kind)) + "\""));

        for (const auto& key : block.keys)
        {
            INFO ("key: " << juce::String (std::string (key.name)));
            CHECK (json.contains ("\"name\": \"" + juce::String (std::string (key.name)) + "\""));
            ++keys;
        }
    }

    // Control case, in both directions: the walk found something, and the
    // schema is the size a reference is worth generating for.
    REQUIRE (lang::schema().size() > 10);
    REQUIRE (keys > 40);
}
