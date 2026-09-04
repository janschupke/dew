#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <map>
#include <set>

#include "SourceScan.h"

using namespace dew::testing;
using namespace dew;

TEST_CASE ("the source gates can see every source directory", "[build][gate]")
{
    // Several tests enforce a convention by scanning DEW_SOURCE_DIR - no raw
    // font construction today, and more to come. Every one of them is silent
    // when it finds nothing, which is indistinguishable from finding nothing
    // because it was looking in the wrong place.
    //
    // So: a restructure that moves production code out of src/ must fail HERE,
    // loudly, rather than turning every other gate into a no-op that still
    // reports success.
    const auto files = sourceFiles();

    REQUIRE (files.size() > 90);

    // Paths, not names. A canary that only knows what a file is CALLED is
    // satisfied by a file of that name anywhere, which is the opposite of
    // checking that the walk still reaches the places it is meant to: these
    // eight are one per layer, and the layer is the half that matters.
    for (const auto* expected :
         { "ui/design/Tokens.h", "ui/primitives/DewControls.cpp", "ui/PianoRollComponent.cpp",
           "engine/AudioEngine.cpp", "model/ProjectSchema.cpp", "app/Settings.cpp",
           "engine/modules/EffectModules.cpp", "ui/MainComponent.cpp" })
    {
        INFO ("expected " << expected << " under DEW_SOURCE_DIR");
        REQUIRE (std::any_of (files.begin(), files.end(), [expected] (const juce::File& f)
                              { return relativePathOf (f) == expected; }));
    }
}

TEST_CASE ("every compiled source is one the gates can see", "[build][gate]")
{
    // The compiled-sources file is the six libraries' own SOURCES lists, written
    // out by CMake at generate time. DEW_SOURCE_DIR is a directory that is merely named src.
    //
    // They agree today. The day they stop - a layer moved out, a generated file
    // compiled from the build tree - every gate built on the directory walk
    // silently stops covering it and keeps reporting success. This is the test
    // that refuses to let that be quiet.
    const juce::File list { DEW_COMPILED_SOURCES_FILE };
    REQUIRE (list.existsAsFile());

    juce::StringArray compiled;
    compiled.addLines (list.loadFileAsString());
    compiled.removeEmptyStrings();

    juce::StringArray walked;

    for (const auto& f : sourceFiles())
        walked.add (relativePathOf (f));

    juce::StringArray missing;
    auto ours = 0;

    for (const auto& line : compiled)
    {
        // Every line is "<layer> <source>"; the layering gate below needs the
        // label, and this one only needs what follows it.
        const auto source = line.fromFirstOccurrenceOf (" ", false, false);

        // JUCE puts its own module sources into every target that links a
        // module, and juce_add_binary_data generates into the build tree. Both
        // arrive here as ABSOLUTE paths; dew's own sources are listed relative
        // to src/, which is exactly the distinction we want.
        if (juce::File::isAbsolutePath (source))
            continue;

        ++ours;

        // The manifest lists a source relative to src/, and so does the walk
        // now, so they are compared as written. This used to drop everything
        // before the last slash and compare base names, which would have let an
        // uncompiled file hide behind a namesake in another layer.
        if (! walked.contains (source))
            missing.add (source);
    }

    INFO ("dew sources found in the libraries: " << ours);
    CHECK (ours > 60);

    INFO ("compiled but not seen by the source gates:\n" << missing.joinIntoString ("\n"));
    CHECK (missing.isEmpty());
}

TEST_CASE ("the code-line reader knows a comment from a quoted one", "[build][gate]")
{
    // The size gate is only as honest as this, and it was not honest. The
    // reader tracked block comments but not STRINGS, so a line quoting the
    // characters that open a comment opened one - and the run of "code" that
    // followed was swallowed until some later literal happened to close it.
    //
    // The file it was blindest to was this one. Every gate that scanned for a
    // comment marker quoted one in its own predicate, so SourceGateTests.cpp
    // reported 301 code lines while really holding 667: two thirds of the gates
    // were invisible to the gate that measures length, and the file sat 267
    // lines over a limit it was itself enforcing on everything else.
    const auto file = juce::File::createTempFile (".cpp");

    file.replaceWithText ("int kept = 1;\n"
                          "// dropped\n"
                          "/* dropped\n"
                          "   dropped, and with no leading asterisk - which is\n"
                          "   how dew writes a doc comment */\n"
                          "auto marker = \"/*\";\n"
                          "int alsoKept = 2;\n");

    const auto code = codeLinesWithNumbersOf (file);
    file.deleteFile();

    REQUIRE (code.size() == 3);

    CHECK (code[0].number == 1);
    CHECK (code[0].text.contains ("int kept"));

    // The line that used to open a comment that never closed.
    CHECK (code[1].number == 6);
    CHECK (code[1].text.contains ("marker"));

    // Proof it closed: without string tracking, this line is inside a comment.
    CHECK (code[2].number == 7);
    CHECK (code[2].text.contains ("alsoKept"));
}

TEST_CASE ("no source file is longer than the tree already is", "[build][gate]")
{
    // 400 lines of CODE - comments and blanks removed by codeLinesOf.
    //
    // The number is measured, not chosen. It is the tree's own p90: nine files
    // in ten were already under it when this gate was written, and the median
    // is 108. It is the same reasoning .clang-format gives for ColumnLimit 100
    // - "the tree's own p99 line is 96 characters, so 100 is a description
    // rather than a new rule".
    //
    // CODE lines rather than raw ones, and that distinction is the whole reason
    // the number is usable. dew's headers carry long doc comments by design -
    // PlaylistComponent.h is 512 lines and 173 of them are code - so a raw
    // count would punish exactly the documentation the house style asks for,
    // and would let a dense file with none of it through.
    //
    // There is NO exemption list, deliberately. A gate with one is a ratchet
    // somebody edits; this one is a rule. Getting here took thirty commits and
    // the largest file in the tree went from 1,740 lines to 449 - so if a file
    // cannot reasonably get under 400, that is an argument about the number,
    // to be had once and in the open, rather than a quiet entry in a list.
    //
    // Tests and tools are held to it too. A 1,788-line test file is exactly as
    // hard to find your way around as a 1,788-line editor, and this codebase
    // had both.
    constexpr int maximumCodeLines = 400;

    juce::StringArray tooLong;
    auto scanned = 0;

    for (const auto& file : allDewFiles())
    {
        ++scanned;

        if (const auto lines = codeLinesOf (file).size(); lines > maximumCodeLines)
            tooLong.add (file.getFileName() + "  " + juce::String (lines) + " code lines");
    }

    // A gate that scanned nothing passes silently, which is the failure mode
    // every other gate here is written to avoid.
    INFO ("scanned " << scanned << " files");
    REQUIRE (scanned > 300);

    INFO ("files over " << maximumCodeLines << " code lines:\n" << tooLong.joinIntoString ("\n"));
    CHECK (tooLong.isEmpty());
}

TEST_CASE ("every source under src is one a library compiles", "[build][gate]")
{
    // The other direction of "every compiled source is one the gates can see"
    // above, which checks manifest -> walk. This checks walk -> manifest.
    //
    // A .cpp that sits under src/ and is in no library's SOURCES is never
    // compiled, and passes every other gate in this file silently: they all
    // scan the directory, so an un-built file is scanned and found clean. That
    // was harmless while nobody added files. The work that split this tree
    // added ninety, several of them in one commit, and forgetting one line of
    // CMakeLists would have meant deleting code that still appeared to be
    // there.
    const juce::File list { DEW_COMPILED_SOURCES_FILE };
    REQUIRE (list.existsAsFile());

    juce::StringArray compiled;
    compiled.addLines (list.loadFileAsString());

    juce::StringArray compiledPaths;

    for (const auto& line : compiled)
        compiledPaths.add (line.fromFirstOccurrenceOf (" ", false, false));

    juce::StringArray orphans;

    for (const auto& file : sourceFiles())
    {
        // Headers are not compiled on their own, and the application target is
        // not in the manifest's foreach - see the layering gate BELOW, which
        // exempts the same two by name.
        if (file.getFileExtension() != ".cpp")
            continue;

        const auto name = file.getFileName();

        if (name == "main.cpp" || name.startsWith ("DewApplication"))
            continue;

        const auto path = relativePathOf (file);

        if (! compiledPaths.contains (path))
            orphans.add (path);
    }

    INFO ("under src/ but in no library's SOURCES - never compiled:\n"
          << orphans.joinIntoString ("\n"));
    CHECK (orphans.isEmpty());
}

TEST_CASE ("every layer is represented in the scanned sources", "[build][gate]")
{
    // Named directories rather than a count, so moving one layer out cannot be
    // masked by another growing.
    //
    // Matched on the path relative to src/, not the absolute one. This asked
    // whether the full path CONTAINED "/ui/", which a checkout living in a
    // directory called ui satisfies without a single dew source being there.
    for (const auto* layer : { "lang", "model", "engine", "io", "ui", "app" })
    {
        auto seen = false;

        for (const auto& f : sourceFiles())
            if (relativePathOf (f).startsWith (juce::String (layer) + "/"))
                seen = true;

        INFO ("no scanned source lives under src/" << layer);
        REQUIRE (seen);
    }
}

TEST_CASE ("no source file includes another layer by relative path", "[build][gate]")
{
    // Rooted includes only: "model/Ids.h", never "../model/Ids.h".
    //
    // A relative-parent include hard-codes a file's position in the tree, which
    // is exactly what moving a layer changes - so a hundred of them are a
    // hundred edits standing between the source tree and any restructure.
    // src/ is already on the include path, so the rooted form has always
    // worked; tools/ and tests/ have always used it.
    const auto found = offenders ([] (const juce::String& line)
                                  { return line.trim().startsWith ("#include \"../"); });

    INFO ("relative-parent includes:\n" << found.joinIntoString ("\n"));
    CHECK (found.isEmpty());
}

TEST_CASE ("the score language layer knows nothing of JUCE", "[build][layering]")
{
    // dew_lang links no JUCE, so a leak would normally be a link error - but a
    // header-only one is not. juce::String would compile, and the first thing it
    // would cost is diagnostics reported in CHARACTER indices where the editor
    // needs BYTE offsets; the second is juce::Random becoming part of the file
    // format the moment a seeded score is rendered.
    //
    // So the rule is checked at the source, where it is actually stated.
    juce::StringArray found;

    const juce::File langDir { juce::String (DEW_SOURCE_DIR) + "/lang" };
    REQUIRE (langDir.isDirectory());

    auto scanned = 0;

    for (const auto& entry : juce::RangedDirectoryIterator (langDir, true, "*.cpp;*.h"))
    {
        const auto file = entry.getFile();
        ++scanned;

        // Comments are stripped before the check, because these files
        // legitimately TALK about juce::CodeDocument::Iterator - the shared
        // scanner's other instantiation lives in the UI layer, and the comment
        // saying so is what makes the design legible. A gate that reads its own
        // prose reports the documentation as the violation.
        //
        // The stripping lives in SourceScan.h, where every other gate reads it
        // too. It used to be written out here as well, fifty lines of it, for
        // the sole reason that codeLinesOf threw away the line number this gate
        // needs to report.
        for (const auto& code : codeLinesWithNumbersOf (file))
            if (code.text.contains ("#include <juce") || code.text.contains ("juce::"))
                found.add (file.getFileName() + ":" + juce::String (code.number) + "  "
                           + code.text.trim());
    }

    // A gate that scanned nothing passes silently, which is the failure mode
    // every other gate here is written to avoid.
    INFO ("scanned " << scanned << " files under src/lang");
    REQUIRE (scanned > 0);

    INFO ("JUCE in the language layer:\n" << found.joinIntoString ("\n"));
    CHECK (found.isEmpty());
}

TEST_CASE ("the engine layer opens no files and no devices", "[build][layering]")
{
    // The property the io split exists to create, stated as a test rather than
    // left to whoever edits src/CMakeLists.txt next.
    //
    // An engine that cannot reach a device or the filesystem is one the whole
    // test suite can drive with neither - which is why 633 tests run in CI with
    // no audio hardware - and it is the precondition for ever wrapping this
    // engine as a plugin, since a plugin must not go looking at the filesystem
    // on its host's behalf.
    const auto found = offenders (
        [] (const juce::String& line)
        {
            const auto trimmed = line.trim();

            if (! trimmed.startsWith ("#include"))
                return false;

            return trimmed.contains ("juce_audio_devices")
                   || trimmed.contains ("juce_audio_formats") || trimmed.contains ("\"io/");
        });

    // A PREFIX, not a parent directory. This asked whether the file's immediate
    // parent was named engine, so src/engine/modules - EffectModules and
    // Instruments, four files - sat outside a gate about the engine layer for
    // as long as that directory has existed. It also matched offenders back to
    // files by base name, which offenders() no longer requires and which was
    // never safe: two files may share a name, and nothing says otherwise.
    juce::StringArray fromEngine;

    for (const auto& offender : found)
        if (offender.startsWith ("engine/"))
            fromEngine.add (offender);

    // Proof the widening is real rather than a comment: the walk reaches past
    // the layer's own directory.
    auto beneathEngine = 0;

    for (const auto& f : sourceFiles())
        if (relativePathOf (f).startsWith ("engine/modules/"))
            ++beneathEngine;

    INFO ("files under src/engine/modules the gate now covers: " << beneathEngine);
    REQUIRE (beneathEngine > 0);

    INFO ("engine sources reaching for a device or a file:\n" << fromEngine.joinIntoString ("\n"));
    CHECK (fromEngine.isEmpty());
}

TEST_CASE ("no source includes the same header twice", "[build][gate]")
{
    // AudioEngine.h included engine/InstrumentModule.h on two consecutive
    // lines. Harmless - the include guard makes the second a no-op - which is
    // exactly why it survived: nothing warns, nothing breaks, and it is
    // invisible unless you are reading the include block itself.
    juce::StringArray repeated;

    for (const auto& file : sourceFiles())
    {
        juce::StringArray lines;
        lines.addLines (file.loadFileAsString());

        juce::StringArray seen;

        for (int i = 0; i < lines.size(); ++i)
        {
            const auto trimmed = lines[i].trim();

            if (! trimmed.startsWith ("#include"))
                continue;

            if (seen.contains (trimmed))
                repeated.add (file.getFileName() + ":" + juce::String (i + 1) + "  " + trimmed);
            else
                seen.add (trimmed);
        }
    }

    INFO ("headers included more than once in one file:\n" << repeated.joinIntoString ("\n"));
    CHECK (repeated.isEmpty());
}

TEST_CASE ("no layer includes a header a layer above it owns", "[build][layering]")
{
    // The general form of the two gates above, and the one that catches what
    // they cannot.
    //
    // The libraries exist so "a layering mistake is a link error". That is true
    // only of a mistake that needs a SYMBOL: two files in dew_design included
    // ui/Gestures.h - a dew_ui header - and linked cleanly, because Gestures.h
    // is header-only and there was nothing to resolve. A rule that fires only
    // when the linker fires does not cover the header-only case.
    //
    // Which library owns a file is not something the directory knows and not
    // something this test should guess. CMake knows, so CMake says: every line
    // of compiled-sources.txt is "<layer> <source>".
    const juce::File list { DEW_COMPILED_SOURCES_FILE };
    REQUIRE (list.existsAsFile());

    juce::StringArray lines;
    lines.addLines (list.loadFileAsString());
    lines.removeEmptyStrings();

    // What each layer may reach, transcribed from src/CMakeLists.txt. Direct
    // dependencies only; the closure below does the rest. A DAG rather than a
    // ladder, because dew_design and dew_app are siblings that know nothing of
    // each other - a rank would let one include the other and say nothing.
    const std::map<juce::String, juce::StringArray> directDeps {
        { "dew_lang", {} },
        { "dew_model", { "dew_lang" } },
        { "dew_engine", { "dew_model" } },
        { "dew_io", { "dew_engine" } },
        { "dew_design", { "dew_engine" } },
        { "dew_app", { "dew_model" } },
        { "dew_ui", { "dew_design", "dew_app", "dew_io" } },
    };

    std::map<juce::String, std::set<juce::String>> mayReach;

    for (const auto& entry : directDeps)
    {
        std::set<juce::String> reached;
        juce::StringArray pending { entry.second };

        while (! pending.isEmpty())
        {
            const auto next = pending[0];
            pending.remove (0);

            if (! reached.insert (next).second)
                continue;

            pending.addArray (directDeps.at (next));
        }

        reached.insert (entry.first); // its own headers, always
        mayReach[entry.first] = reached;
    }

    // Directory -> owning layer, built from what CMake compiled. A directory
    // holding sources of two libraries is itself the defect: it means the tree
    // stopped saying which library a header beside them belongs to.
    std::map<juce::String, juce::String> layerOfDirectory;
    juce::StringArray ambiguous;

    for (const auto& line : lines)
    {
        const auto layer = line.upToFirstOccurrenceOf (" ", false, false);
        const auto source = line.fromFirstOccurrenceOf (" ", false, false);

        // JUCE's own module sources arrive absolute, in every target that links
        // a module. dew's are relative to src/.
        if (juce::File::isAbsolutePath (source))
            continue;

        const auto directory = source.contains ("/")
                                   ? source.upToLastOccurrenceOf ("/", false, false)
                                   : juce::String ("");

        const auto existing = layerOfDirectory.find (directory);

        if (existing == layerOfDirectory.end())
            layerOfDirectory[directory] = layer;
        else if (existing->second != layer)
            ambiguous.add ("src/" + directory + " holds sources of both " + existing->second
                           + " and " + layer);
    }

    ambiguous.removeDuplicates (false);
    INFO ("directories owned by more than one library:\n" << ambiguous.joinIntoString ("\n"));
    CHECK (ambiguous.isEmpty());

    REQUIRE (layerOfDirectory.size() > 5);

    juce::StringArray climbing;

    for (const auto& file : sourceFiles())
    {
        // main.cpp and the DewApplication files belong to the `dew` application
        // target, which sits above every library and is not in the foreach that
        // writes the list. They share src/ with dew_model's BuildInfo.cpp, so
        // the directory cannot speak for them.
        //
        // The prefix has no trailing dot, and that matters: it used to, and
        // DewApplicationMenus.cpp was judged as dew_model the moment it existed
        // - every ui/ include in it an offence. Any DewApplication* file is the
        // application's.
        const auto name = file.getFileName();

        if (name == "main.cpp" || name.startsWith ("DewApplication"))
            continue;

        const auto relative = file.getRelativePathFrom (juce::File { DEW_SOURCE_DIR })
                                  .replaceCharacter ('\\', '/');

        const auto directory = relative.contains ("/")
                                   ? relative.upToLastOccurrenceOf ("/", false, false)
                                   : juce::String ("");

        const auto owner = layerOfDirectory.find (directory);

        if (owner == layerOfDirectory.end())
            continue;

        const auto& reachable = mayReach.at (owner->second);

        juce::StringArray fileLines;
        fileLines.addLines (file.loadFileAsString());

        for (int i = 0; i < fileLines.size(); ++i)
        {
            const auto trimmed = fileLines[i].trim();

            if (! trimmed.startsWith ("#include \""))
                continue;

            const auto included = trimmed.fromFirstOccurrenceOf ("\"", false, false)
                                      .upToFirstOccurrenceOf ("\"", false, false);

            if (! included.contains ("/"))
                continue;

            const auto includedDirectory = included.upToLastOccurrenceOf ("/", false, false);
            const auto includedOwner = layerOfDirectory.find (includedDirectory);

            if (includedOwner == layerOfDirectory.end())
                continue;

            if (reachable.count (includedOwner->second) == 0)
                climbing.add (name + ":" + juce::String (i + 1) + "  " + owner->second
                              + " includes " + included + ", which " + includedOwner->second
                              + " owns");
        }
    }

    INFO ("includes that climb the layering:\n" << climbing.joinIntoString ("\n"));
    CHECK (climbing.isEmpty());
}
