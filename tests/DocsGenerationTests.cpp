#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <juce_core/juce_core.h>

#include "ui/design/Theme.h"
#include "DocsMcp.h"
#include "DocsSamples.h"
#include "DocsSchema.h"
#include "DocsTokens.h"

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

TEST_CASE ("the website's MCP reference is what the emitter writes", "[docs][website][gate][mcp]")
{
    const auto file = generatedFile ("mcp-tools.json");

    INFO ("file: " << file.getFullPathName());
    INFO ("regenerate: cmake --build --preset ci --target dew_mcp && "
          "./build/ci/tools/dew_mcp_artefacts/RelWithDebInfo/dew_mcp schema "
          "website/src/generated/mcp-tools.json");

    REQUIRE (file.existsAsFile());
    CHECK (file.loadFileAsString().toStdString() == docs::mcpJson());
}

TEST_CASE ("the MCP emitter writes the same bytes twice", "[docs][website][gate][mcp]")
{
    CHECK (docs::mcpJson() == docs::mcpJson());
}

TEST_CASE ("every operation and every guide page reaches the reference",
           "[docs][website][gate][mcp]")
{
    // The half that a byte comparison cannot catch: the file agreeing with the
    // emitter says nothing about the emitter agreeing with the TABLE. An
    // operation the walk skipped would be missing from both, identically.
    const auto emitted = docs::mcpJson();

    for (const auto& op : control::ops())
    {
        INFO ("operation: " << op.name);
        CHECK (emitted.find (std::string ("\"") + op.name + "\"") != std::string::npos);
    }

    for (const auto& section : control::guide())
    {
        INFO ("guide section: " << section.id);
        CHECK (emitted.find (std::string ("\"") + section.id + "\"") != std::string::npos);
    }
}

TEST_CASE ("the schema is written in the reference locale, whatever is asked for",
           "[docs][website][gate][i18n]")
{
    // The schema's prose - every block's doc, every key's doc, every kind's
    // noun phrase - now comes from the catalogue, and score-schema.json is
    // committed and compared byte for byte in three places. So the ONE thing
    // that must stay true is that the artefact is a function of the schema and
    // never of a locale.
    //
    // dew_docs passes no locale and takes the reference default, which is what
    // keeps it so. This says the coupling out loud rather than leaving it as a
    // property of a default argument nobody looks at, and it is the test that
    // fails the day somebody threads a --locale flag through without deciding
    // what the committed file should hold.
    CHECK (docs::schemaJson() == docs::schemaJson (lang::referenceLocale));

    // A locale this build does not carry falls back to the reference rather
    // than to blank rows, so asking for one cannot silently empty the
    // reference manual.
    CHECK (docs::schemaJson (lang::localeFor ("de-CH")) == docs::schemaJson());

    // Control case: the emitter is reading the catalogue at all. Both of the
    // above would pass over a file of empty strings.
    INFO (docs::schemaJson().substr (0, 400));
    CHECK (docs::schemaJson().find ("a tempo, like `96 bpm`") != std::string::npos);
    CHECK (docs::schemaJson().find ("whole-song properties") != std::string::npos);
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

TEST_CASE ("the website's design tokens are what the emitter writes", "[docs][website][gate]")
{
    const auto file = generatedFile ("design-tokens.json");

    INFO ("file: " << file.getFullPathName());
    INFO ("regenerate: cmake --build --preset ci --target dew_shot && "
          "./build/ci/tools/dew_shot_artefacts/RelWithDebInfo/dew_shot tokens "
          "website/src/generated/design-tokens.json");

    REQUIRE (file.existsAsFile());
    CHECK (file.loadFileAsString().toStdString() == docs::tokensJson());
}

TEST_CASE ("the token emitter writes the same bytes twice", "[docs][website][gate]")
{
    CHECK (docs::tokensJson() == docs::tokensJson());
}

TEST_CASE ("the emitted tokens are the dark palette, whatever theme is in force",
           "[docs][website][gate]")
{
    // darkPalette() directly rather than colour::active, so the emitter does
    // not depend on whether anything called theme::applyPalette first. dew_shot
    // calls it for every other mode, and a token file that changed with a
    // --theme flag would be a file whose content depended on how it was asked
    // for.
    const auto before = docs::tokensJson();

    theme::applyPalette (theme::Kind::highContrast);
    const auto during = docs::tokensJson();
    theme::applyPalette (theme::Kind::dark);

    CHECK (before == during);
}

TEST_CASE ("a lift is the colour the app actually paints", "[docs][website][gate]")
{
    // juce::Colour::brighter is 255 - (1/(1+amount)) * (255 - channel) per
    // sRGB channel, TRUNCATED to a uint8. CSS color-mix rounds, and "6%
    // lighter" is a third answer again - so the composed values are emitted
    // rather than recomputed on the web, and this pins two of them.
    const auto palette = tokens::colour::darkPalette();

    // A hovered `normal` button. NOT colour::surfaceHover, which is a darker
    // colour doing a different job - the trap a web control reaching for the
    // token whose name says "hover" would fall into.
    CHECK (docs::cssColour (palette.surfaceRaised.brighter (tokens::emphasis::controlLift))
           == "#3e4148");
    CHECK (docs::cssColour (palette.surfaceHover) == "#343941");

    const auto json = juce::String (docs::tokensJson());
    CHECK (json.contains ("\"surfaceRaisedHover\": \"#3e4148\""));
}

TEST_CASE ("the website's score samples are what the emitter writes", "[docs][website][gate]")
{
    // The site renders these instead of highlighting anything, so a stale copy
    // is a page whose colours disagree with the editor's - which is the exact
    // thing .ai/rules/score-language.md refuses.
    const auto file = generatedFile ("score-samples.json");

    INFO ("file: " << file.getFullPathName());
    INFO ("regenerate: ./build/ci/tools/dew_shot_artefacts/RelWithDebInfo/dew_shot samples "
          "website/src/generated/score-samples.json examples/halcyon.score "
          "examples/rhodes.score examples/ironmeter.score examples/nightfall.score");

    REQUIRE (file.existsAsFile());

    juce::Array<juce::File> scores;

    for (const auto* name :
         { "halcyon.score", "rhodes.score", "ironmeter.score", "nightfall.score" })
        scores.add (juce::File { juce::String (DEW_EXAMPLES_DIR) }.getChildFile (name));

    CHECK (file.loadFileAsString().toStdString() == docs::samplesJson (scores));
}

TEST_CASE ("a sample's runs reconstruct its source exactly", "[docs][website][gate]")
{
    // The claim the website rests on. It renders the runs and the gaps between
    // them, so if the two do not add up to the source the page shows something
    // the file does not say - and this is the only place that can tell.
    const auto json = juce::JSON::parse (generatedFile ("score-samples.json").loadFileAsString());

    const auto* samples = json.getArray();
    REQUIRE (samples != nullptr);
    REQUIRE (samples->size() == 4);

    for (const auto& entry : *samples)
    {
        const auto source = entry.getProperty ("source", {}).toString();
        const auto* runs = entry.getProperty ("runs", {}).getArray();

        INFO ("sample: " << entry.getProperty ("name", {}).toString());
        REQUIRE (runs != nullptr);
        REQUIRE (runs->size() > 100);

        // In order, non-overlapping, and inside the file.
        auto at = 0;

        for (const auto& run : *runs)
        {
            const auto offset = (int) run.getProperty ("offset", {});
            const auto length = (int) run.getProperty ("length", {});

            CHECK (offset >= at);
            CHECK (length > 0);
            at = offset + length;
        }

        CHECK (at <= source.length());
    }
}
