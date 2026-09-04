#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <map>
#include <set>

#include "SourceScan.h"
#include "model/AutomationTargets.h"
#include "model/ModuleCatalog.h"
#include "ui/design/Tokens.h"

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

    for (const auto* expected :
         { "Tokens.h", "DewControls.cpp", "PianoRollComponent.cpp", "AudioEngine.cpp",
           "ProjectSchema.cpp", "Settings.cpp", "EffectModules.cpp", "MainComponent.cpp" })
    {
        INFO ("expected a file named " << expected << " under DEW_SOURCE_DIR");
        REQUIRE (std::any_of (files.begin(), files.end(), [expected] (const juce::File& f)
                              { return f.getFileName() == expected; }));
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
        walked.add (f.getFileName());

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

        const auto name = source.fromLastOccurrenceOf ("/", false, false);

        if (! walked.contains (name))
            missing.add (source);
    }

    INFO ("dew sources found in the libraries: " << ours);
    CHECK (ours > 60);

    INFO ("compiled but not seen by the source gates:\n" << missing.joinIntoString ("\n"));
    CHECK (missing.isEmpty());
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

    juce::StringArray names;

    for (const auto& line : compiled)
    {
        const auto source = line.fromFirstOccurrenceOf (" ", false, false);
        names.add (source.fromLastOccurrenceOf ("/", false, false));
    }

    juce::StringArray orphans;

    for (const auto& file : sourceFiles())
    {
        // Headers are not compiled on their own, and the application target is
        // not in the manifest's foreach - see the layering gate above, which
        // exempts the same two by name.
        if (file.getFileExtension() != ".cpp")
            continue;

        const auto name = file.getFileName();

        if (name == "main.cpp" || name.startsWith ("DewApplication"))
            continue;

        if (! names.contains (name))
            orphans.add (name);
    }

    INFO ("under src/ but in no library's SOURCES - never compiled:\n"
          << orphans.joinIntoString ("\n"));
    CHECK (orphans.isEmpty());
}

TEST_CASE ("every layer is represented in the scanned sources", "[build][gate]")
{
    // Named directories rather than a count, so moving one layer out cannot be
    // masked by another growing.
    for (const auto* layer : { "lang", "model", "engine", "io", "ui", "app" })
    {
        auto seen = false;

        for (const auto& f : sourceFiles())
            if (f.getParentDirectory().getFileName() == layer
                || f.getFullPathName().contains (juce::String ("/") + layer + "/"))
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

        juce::StringArray lines;
        lines.addLines (file.loadFileAsString());

        // Comments are stripped before the check, because these files
        // legitimately TALK about juce::CodeDocument::Iterator - the shared
        // scanner's other instantiation lives in the UI layer, and the comment
        // saying so is what makes the design legible. A gate that reads its own
        // prose reports the documentation as the violation.
        //
        // Tracked across lines rather than per line, because dew's doc comments
        // continue without a leading asterisk, so "starts with *" sees the body
        // of every /** */ block as code.
        auto inBlockComment = false;

        for (int i = 0; i < lines.size(); ++i)
        {
            auto code = lines[i];

            if (inBlockComment)
            {
                const auto closes = code.indexOf ("*/");

                if (closes < 0)
                    continue;

                code = code.substring (closes + 2);
                inBlockComment = false;
            }

            if (const auto opens = code.indexOf ("/*"); opens >= 0)
            {
                const auto closes = code.indexOf (opens + 2, "*/");

                if (closes < 0)
                {
                    code = code.substring (0, opens);
                    inBlockComment = true;
                }
                else
                {
                    code = code.substring (0, opens) + code.substring (closes + 2);
                }
            }

            if (const auto lineComment = code.indexOf ("//"); lineComment >= 0)
                code = code.substring (0, lineComment);

            if (code.contains ("#include <juce") || code.contains ("juce::"))
                found.add (file.getFileName() + ":" + juce::String (i + 1) + "  " + code.trim());
        }
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

    juce::StringArray fromEngine;

    for (const auto& offender : found)
    {
        // offenders() reports by file NAME, so ask the walk where it lives.
        for (const auto& f : sourceFiles())
            if (offender.startsWith (f.getFileName() + ":")
                && f.getParentDirectory().getFileName() == "engine")
                fromEngine.add (offender);
    }

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

/** True when `line` hands a non-ASCII literal to juce::String's const char*
    constructor, which is the one place JUCE decodes those bytes as ASCII.

    Hoisted out of the gate so the gate can be pointed at a line it should catch
    and a line it should not, which is the only way to know a scanner works.
*/
static bool feedsAsciiConstructor (const juce::String& line)
{
    const auto trimmed = line.trim();

    if (trimmed.startsWith ("//") || trimmed.startsWith ("*") || trimmed.startsWith ("/*"))
        return false;

    for (int i = 0; i < line.length();)
    {
        if (line[i] != '"')
        {
            ++i;
            continue;
        }

        const auto open = i;
        auto close = -1;

        for (int j = i + 1; j < line.length(); ++j)
        {
            if (line[j] == '\\')
            {
                ++j;
                continue;
            }

            if (line[j] == '"')
            {
                close = j;
                break;
            }
        }

        if (close < 0)
            return false;

        auto holdsNonAscii = false;

        for (int j = open + 1; j < close; ++j)
            if (line[j] > 127)
                holdsNonAscii = true;

        if (holdsNonAscii)
        {
            const auto before = line.substring (0, open).trimEnd();
            const auto after = line.substring (close + 1).trim();

            // Opening a concatenation means the literal is the LEFT operand, so
            // operator+ (const char*, const String&) builds a String from it
            // first. A literal already on the right of a + goes through
            // operator+=, which reads UTF-8 and is correct.
            if (after.startsWith ("+") && ! before.endsWith ("+") && ! before.endsWith ("<<"))
                return true;

            // The same constructor, reached directly. String (CharPointer_UTF8
            // ("...")) is the escape hatch and does not match: it does not end
            // in "String (".
            if (before.endsWith ("juce::String (") || before.endsWith ("juce::String(")
                || before.endsWith ("String (") || before.endsWith ("String("))
                return true;
        }

        i = close + 1;
    }

    return false;
}

TEST_CASE ("no source declares a bare juce::ComboBox", "[build][gate]")
{
    // Same rule as the checkbox above, and the same reason: DewLookAndFeel
    // paints the box, the arrow and the menu, so what a stock ComboBox lacks is
    // not an appearance but a cursor - and JUCE does not inherit one from a
    // parent, so a dropdown left alone shows an arrow beside a button showing a
    // hand. Sixteen boxes in seven panels is past the count at which a habit
    // stays reliable.
    //
    // Declarations only. A function taking a juce::ComboBox& is taking the base
    // class of a DewDropdown, which is correct.
    const auto found = offenders (
        [] (const juce::String& line)
        {
            const auto trimmed = line.trim();

            if (trimmed.startsWith ("//") || trimmed.startsWith ("*") || trimmed.startsWith ("/*"))
                return false;

            return trimmed.startsWith ("juce::ComboBox ") || trimmed.contains ("juce::ComboBox>()")
                   || trimmed.contains ("new juce::ComboBox");
        },
        { "DewControls.h" });

    INFO ("stock combo boxes:\n" << found.joinIntoString ("\n"));
    CHECK (found.isEmpty());
}

TEST_CASE ("no source names a mouse cursor outside the vocabulary", "[build][gate][design]")
{
    // Nine call sites picked a juce::MouseCursor by hand, and two of them
    // disagreed about what a draggable thing looks like: the effect chain's
    // grip said DraggingHand and the playlist's clips said nothing at all, so
    // the gesture a person uses most had no feedback while a rarer one did.
    //
    // Cursors.h names them for the GESTURE - clickable, value, move, resizeX,
    // nib, idle - the same way the colours are named for their role.
    const auto found = offenders (
        [] (const juce::String& line)
        {
            const auto trimmed = line.trim();

            if (trimmed.startsWith ("//") || trimmed.startsWith ("*") || trimmed.startsWith ("/*"))
                return false;

            return line.contains ("juce::MouseCursor::");
        },
        { "Cursors.h" });

    INFO ("cursors chosen outside the vocabulary:\n" << found.joinIntoString ("\n"));
    CHECK (found.isEmpty());
}

TEST_CASE ("no source declares a bare juce::ToggleButton", "[build][gate]")
{
    // juce::Button completes a click for whichever mouse button pressed it, so
    // a stock ToggleButton flips on a right-click - which is a gesture that in
    // every other part of dew means "show me a menu" and never means "do it".
    // DewCheckbox is that control with the press filtered, and the five that
    // were stock lived in two dialogs where nobody thought to check.
    //
    // Declarations only: the LookAndFeel names ToggleButton's colour ids, and
    // theming the stock control is the reason DewCheckbox does not repaint it.
    const auto found = offenders (
        [] (const juce::String& line)
        {
            const auto trimmed = line.trim();

            if (trimmed.startsWith ("//") || trimmed.startsWith ("*") || trimmed.startsWith ("/*"))
                return false;

            return trimmed.startsWith ("juce::ToggleButton ")
                   || trimmed.contains ("juce::ToggleButton>");
        },
        { "DewControls.h" });

    INFO ("stock toggle buttons:\n" << found.joinIntoString ("\n"));
    CHECK (found.isEmpty());
}

TEST_CASE ("no source starts a concatenation with a non-ASCII literal", "[build][gate]")
{
    // juce::String decodes an 8-bit literal two different ways depending on
    // which side of the + it is on, and nothing warns:
    //
    //     String::String (const char*)      -> CharPointer_ASCII, mangles UTF-8
    //     String::operator+= (const char*)  -> CharPointer_UTF8,  correct
    //
    // So `name + " — "` is right and `" — " + name` is not, and the window title
    // was the second one: "dew — Untitled" reached the title bar with the em
    // dash split into three characters. RenderPanel.cpp had already written the
    // rule down in a comment, one file away, and the comment did not stop it.
    //
    // Invisible in review - the same file can hold both forms and only one is
    // wrong - which is exactly the kind of rule that has to be a scanner.
    const auto found = offenders ([] (const juce::String& line)
                                  { return feedsAsciiConstructor (line); });

    INFO ("non-ASCII literals decoded as ASCII:\n" << found.joinIntoString ("\n"));
    CHECK (found.isEmpty());

    // Control case: a scanner that cannot see the defect it was written for is
    // not a gate. Both lines below are the window title, before and after.
    const juce::String dash (juce::CharPointer_UTF8 ("\xe2\x80\x94"));

    CHECK (feedsAsciiConstructor ("setName (\"dew " + dash + " \" + name);"));
    CHECK_FALSE (feedsAsciiConstructor ("title += \" " + dash + " \";"));
    CHECK_FALSE (feedsAsciiConstructor ("summary = seconds + \"  " + dash + "  \" + rate;"));
}

TEST_CASE ("no source spells an automatable parameter as a string literal", "[build][gate]")
{
    // The twenty names that were the actual defect: the snapshot builder held a
    // table mapping "cutoff", "roomSize", "midFreq" and the rest to an enum, and
    // it was the only place in production that wrote a property name by hand.
    //
    // The failure was silent in the worst way. Renaming an identifier in Ids.h
    // compiled cleanly, the schema and the editor followed the new name, and
    // every automation curve pointing at the old one simply stopped doing
    // anything - because a name that matches nothing is indistinguishable from
    // an automation of nothing.
    //
    // Scoped to automatable parameters rather than every identifier, and taken
    // from the catalog rather than scraped, because dew's identifiers also
    // include node types (CHANNEL, MIXER) that are legitimate UI captions, and
    // property names that are also legitimate VALUES - "loop", "record",
    // "wavetable", "drive". Widening this beyond the parameters would be a gate
    // that cries wolf, which is a gate people turn off.
    // ONE call rather than five table lookups written out here. The gate used to
    // walk each per-scope table by hand, so a table added beside them was a
    // table the gate silently did not cover.
    auto names = dew::automatableParameterNames();

    // Two whole files are exempt for the same reason the effect ids below are.
    // Icons.cpp is a registry of ICON names, and "mute" is one of them; src/lang
    // is the score language, whose keywords are its own vocabulary and address
    // its own tree, not the project's. Both became offenders the moment mute
    // turned into an automatable parameter, and neither is the defect this gate
    // exists to catch - which is a property name written by hand where a
    // property is being RESOLVED.
    //
    // An effect's id and one of its parameters share a spelling in one case -
    // "drive" is both - and the id is a value a file legitimately contains.
    for (const auto& descriptor : dew::effectDescriptors())
        names.removeString (descriptor.id);

    // Control case: a gate over an empty list is not a gate.
    REQUIRE (names.size() > 15);
    REQUIRE (names.contains ("cutoff"));
    REQUIRE (names.contains ("midFreq"));

    const auto found = dew::testing::offenders (
        [&names] (const juce::String& line)
        {
            const auto trimmed = line.trim();

            // The exemption is the DIRECTORY src/ui/design/icons, not a file in it.
            // The catalog names its own icons as strings - { "mute", mute } - and
            // several of those names are also parameter names. It was one file and
            // one entry; splitting it into three would have meant three entries,
            // which is the list SourceScan.h warns a fourth file falls off.
            // Doc comments name properties all through this codebase, deliberately.
            if (trimmed.startsWith ("//") || trimmed.startsWith ("*") || trimmed.startsWith ("/*"))
                return false;

            for (const auto& name : names)
                if (line.contains ("\"" + name + "\""))
                    return true;

            return false;
        },
        { "Ids.h", "icons", "lang" });

    INFO ("automatable parameters written as string literals:\n" << found.joinIntoString ("\n"));
    CHECK (found.isEmpty());
}

// =============================================================================
// The design-system gates.
//
// Tokens.h is exempt from all three, because it is where the values are
// declared; a gate that forbade its own definitions would only be forbidding
// the design system from existing.
// =============================================================================

TEST_CASE ("no source names a colour by its hex value", "[build][gate][design]")
{
    // A colour written as 0xff4fa3ff is a colour that no theme can change and
    // that no reader can name. dew had two - the piano keyboard's black and
    // white keys, written inline in the painter that drew them - and they are
    // tokens now.
    //
    // juce::Colour::fromString is deliberately NOT caught: it reads a colour a
    // document stored, which is data, not a design decision.
    const auto found = offenders (
        [] (const juce::String& line)
        {
            const auto trimmed = line.trim();

            if (trimmed.startsWith ("//") || trimmed.startsWith ("*") || trimmed.startsWith ("/*"))
                return false;

            return line.contains ("juce::Colour (0x") || line.contains ("juce::Colour(0x");
        },
        { "Tokens.h", "Tokens.cpp" });

    INFO ("colours written as hex outside the token file:\n" << found.joinIntoString ("\n"));
    CHECK (found.isEmpty());
}

TEST_CASE ("no source states an emphasis as a bare number", "[build][gate][design]")
{
    // The second, undeclared design system: fifteen alphas and nine brighten
    // factors, chosen one at a time, which between them made a twenty-four rung
    // scale nobody had named. Two panels drawn "faintly" were drawn at 0.07 and
    // 0.10 a week apart.
    //
    // 0.0 and 1.0 are allowed: fully transparent and fully opaque are not
    // rungs on a scale, they are the ends of the axis the scale sits on.
    const auto found = offenders (
        [] (const juce::String& line)
        {
            const auto trimmed = line.trim();

            if (trimmed.startsWith ("//") || trimmed.startsWith ("*") || trimmed.startsWith ("/*"))
                return false;

            for (const auto* call :
                 { ".withAlpha (", ".brighter (", ".darker (", ".withSaturation (",
                   ".withBrightness (", ".withMultipliedSaturation (",
                   ".withMultipliedBrightness (" })
            {
                auto rest = line;

                while (rest.contains (call))
                {
                    rest = rest.fromFirstOccurrenceOf (call, false, false);

                    // Just this call's own argument list, so a token argument
                    // followed later in the line by an unrelated number is not a
                    // false positive.
                    const auto argument = rest.upToFirstOccurrenceOf (")", false, false);

                    for (int i = 0; i < argument.length(); ++i)
                    {
                        if (! juce::CharacterFunctions::isDigit (argument[i]))
                            continue;

                        if (i > 0
                            && (juce::CharacterFunctions::isLetterOrDigit (argument[i - 1])
                                || argument[i - 1] == '.' || argument[i - 1] == '_'))
                            continue;

                        const auto number = argument.substring (i).initialSectionContainingOnly (
                            "0123456789.");

                        if (number != "0.0" && number != "1.0" && number != "0" && number != "1")
                            return true;
                    }
                }
            }

            return false;
        },
        { "Tokens.h" });

    INFO ("emphasis written as bare numbers:\n" << found.joinIntoString ("\n"));
    CHECK (found.isEmpty());
}

TEST_CASE ("no source states a radius or a stroke as a bare number", "[build][gate][design]")
{
    // A corner drawn at 4.0 when the scale says 3 or 5 is not a decision, it is
    // a component that was written without the scale open. drawButtonBackground
    // had exactly that.
    //
    // Icons.cpp is exempt: its numbers are path GEOMETRY in the icon's own 0..1
    // space - where 0.22 is a position, not a width - and its stroke weights
    // already come from icon::. A gate that read them as pixel values would be
    // reading a different coordinate system.
    const auto found = offenders (
        [] (const juce::String& line)
        {
            const auto trimmed = line.trim();

            if (trimmed.startsWith ("//") || trimmed.startsWith ("*") || trimmed.startsWith ("/*"))
                return false;

            const auto numberAfterComma = [] (const juce::String& text)
            {
                for (int i = 1; i < text.length(); ++i)
                    if (text[i - 1] == ','
                        && juce::CharacterFunctions::isDigit (text.substring (i).trimStart()[0]))
                        return true;

                return false;
            };

            for (const auto* call : { "RoundedRectangle (", "drawRect (", "drawEllipse (" })
                if (line.contains (call)
                    && numberAfterComma (line.fromFirstOccurrenceOf (call, false, false)))
                    return true;

            auto rest = line;

            while (rest.contains ("PathStrokeType ("))
            {
                rest = rest.fromFirstOccurrenceOf ("PathStrokeType (", false, false);

                if (juce::CharacterFunctions::isDigit (rest.trimStart()[0]))
                    return true;
            }

            return false;
        },
        { "Tokens.h", "icons" });

    INFO ("radii and strokes written as bare numbers:\n" << found.joinIntoString ("\n"));
    CHECK (found.isEmpty());
}

TEST_CASE ("no component redeclares a size the ladder already names", "[build][gate][design]")
{
    // The defect this ends: six horizontal strips declared in six files with
    // nothing relating them - the toolbars said 34, the tab bar 30, the
    // transport bar 46, the status bar 24, the effect chain's heading 26 and a
    // settings row 28 - and three gutters of which exactly one was a token.
    // They read as one application only if they are declared as one ladder.
    //
    // Scoped by NAME as well as by value, because dew's constants include a
    // 24-semitone transpose limit and a 24-band analyser, and a gate that
    // called those strip heights would be a gate somebody turns off. A
    // declaration has to be claiming to be a dimension - to end in Height,
    // Width, Thickness, Depth or Gutter - before its value is compared.
    struct Rung
    {
        int value;
        const char* name;
    };

    const Rung ladder[] {
        { tokens::size::controlHeight, "controlHeight" },
        { tokens::size::controlHeightSm, "controlHeightSm" },
        { tokens::size::iconButton, "iconButton" },
        { tokens::size::knob, "knob" },
        { tokens::size::rowHeight, "rowHeight or stripToolbar" },
        { tokens::size::rulerHeight, "rulerHeight or letterToggle" },
        { tokens::size::stripStatus, "stripStatus or iconButton" },
        { tokens::size::stripHeading, "stripHeading or controlHeight" },
        { tokens::size::stripFormRow, "stripFormRow" },
        { tokens::size::stripTabs, "stripTabs" },
        { tokens::size::stripTransport, "stripTransport" },
        { tokens::size::gutterChannel, "gutterChannel" },
        { tokens::size::gutterTrack, "gutterTrack" },
        { tokens::size::gutterKeyboard, "gutterKeyboard" },
        { tokens::size::gutterLabel, "gutterLabel" },
        { tokens::size::scrollThickness, "scrollThickness or meterHeight" },
        { tokens::size::knobRow, "knobRow" },
        { tokens::size::mixerStripWidth, "mixerStripWidth" },
    };

    const auto found = offenders (
        [&ladder] (const juce::String& line)
        {
            const auto trimmed = line.trim();

            if (! trimmed.startsWith ("constexpr int")
                && ! trimmed.startsWith ("static constexpr int"))
                return false;

            const auto declaration = trimmed.fromFirstOccurrenceOf ("int ", false, false);
            const auto name = declaration.upToFirstOccurrenceOf ("=", false, false).trim();
            const auto value = declaration.fromFirstOccurrenceOf ("=", false, false)
                                   .upToFirstOccurrenceOf (";", false, false)
                                   .trim();

            // A dimension, not a count or a limit.
            auto claimsToBeADimension = false;

            for (const auto* suffix : { "Height", "Width", "Thickness", "Depth", "Gutter" })
                if (name.endsWith (suffix))
                    claimsToBeADimension = true;

            if (! claimsToBeADimension || ! value.containsOnly ("0123456789"))
                return false;

            for (const auto& rung : ladder)
                if (value.getIntValue() == rung.value)
                    return true;

            return false;
        },
        { "Tokens.h" });

    INFO ("dimensions the size ladder already declares:\n" << found.joinIntoString ("\n"));
    CHECK (found.isEmpty());
}

TEST_CASE ("no source states a gap or an inset as a bare number", "[build][gate][design]")
{
    // Two shapes, and only two, because they are the ones where a bare number
    // is unambiguously a gap:
    //
    //   area.removeFromTop (6);          - a slice taken and thrown away
    //   bounds.reduced (10)              - an inset, always
    //
    // A removeFrom whose RESULT is used is a component saying how tall its own
    // title row is, which is its business and not the spacing scale's. That
    // distinction is why this gate needs no allowlist: every line it can see is
    // one the scale should own.
    //
    // Float arguments are out of scope. JUCE's Rectangle<float> overloads take
    // a float, dew's spacing scale is integral, and `reduced (2.0f)` on a float
    // rectangle is a sub-pixel optical inset - a different thing from a gap,
    // and one stroke::whisper already names where it recurs.
    const auto found = offenders (
        [] (const juce::String& line)
        {
            const auto trimmed = line.trim();

            if (trimmed.startsWith ("//") || trimmed.startsWith ("*") || trimmed.startsWith ("/*"))
                return false;

            const auto bareInteger = [] (const juce::String& text)
            {
                const auto argument = text.trimStart();

                if (! juce::CharacterFunctions::isDigit (argument[0]))
                    return false;

                const auto number = argument.initialSectionContainingOnly ("0123456789");

                // A zero inset is not a gap on any scale; it says "not in this
                // direction", which is what `reduced (space::xs, 0)` means.
                return number != "0" && ! argument.substring (number.length()).startsWith (".");
            };

            for (const auto* call : { "reduced (", "expanded (" })
            {
                auto rest = line;

                while (rest.contains (call))
                {
                    rest = rest.fromFirstOccurrenceOf (call, false, false);

                    if (bareInteger (rest))
                        return true;

                    // The second argument too: reduced (x, y) insets both axes.
                    const auto arguments = rest.upToFirstOccurrenceOf (")", false, false);

                    if (arguments.contains (",")
                        && bareInteger (arguments.fromFirstOccurrenceOf (",", false, false)))
                        return true;
                }
            }

            // A slice taken and discarded is a gap, whatever it is called. The
            // whole statement has to BE the call - `area.removeFromTop (6);` - so
            // that `button.setBounds (row.removeFromRight (110))`, where the slice
            // is the button's own width, is not read as one.
            for (const auto* call : { ".removeFromTop (", ".removeFromBottom (",
                                      ".removeFromLeft (", ".removeFromRight (" })
            {
                if (! trimmed.endsWith (");") || ! trimmed.contains (call))
                    continue;

                const auto receiver = trimmed.upToFirstOccurrenceOf (call, false, false);

                if (! receiver.containsOnly ("abcdefghijklmnopqrstuvwxyz"
                                             "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_."))
                    continue;

                if (bareInteger (trimmed.fromFirstOccurrenceOf (call, false, false)))
                    return true;
            }

            return false;
        },
        { "Tokens.h" });

    INFO ("gaps and insets written as bare numbers:\n" << found.joinIntoString ("\n"));
    CHECK (found.isEmpty());
}

TEST_CASE ("no source picks its own refresh rate", "[build][gate][design]")
{
    // A widget that starts a 30Hz timer by literal is a widget that will not
    // follow when the application decides what "a list refresh" costs, and dew
    // had one - the transport bar - beside four others already quoting
    // motion::uiRefreshHz and motion::playheadHz.
    const auto found = offenders (
        [] (const juce::String& line)
        {
            const auto trimmed = line.trim();

            if (trimmed.startsWith ("//") || trimmed.startsWith ("*") || trimmed.startsWith ("/*"))
                return false;

            return trimmed.contains ("startTimerHz (")
                   && juce::CharacterFunctions::isDigit (
                       trimmed.fromFirstOccurrenceOf ("startTimerHz (", false, false)[0]);
        },
        { "Tokens.h" });

    INFO ("timers started at a rate of their own choosing:\n" << found.joinIntoString ("\n"));
    CHECK (found.isEmpty());
}

TEST_CASE ("every token the design system declares is one the app uses", "[build][gate][design]")
{
    // A token nobody references is a claim the code does not back. radius::pill
    // was one: the design system said dew had pill shapes, dew had none, and a
    // reader looking for one would have gone looking for a component that does
    // not exist. The eight-entry channel ramp was another - it was the DESIGNED
    // colour scheme and the application quietly used a four-entry copy instead,
    // which is the more expensive shape of the same mistake.
    //
    // This reads Tokens.h for what it declares and the whole tree - src, tests
    // and tools - for what quotes it.
    const juce::File tokensFile { juce::String (DEW_SOURCE_DIR) + "/ui/design/Tokens.h" };
    REQUIRE (tokensFile.existsAsFile());

    juce::StringArray declared;
    juce::StringArray lines;
    lines.addLines (tokensFile.loadFileAsString());

    for (const auto& line : lines)
    {
        const auto trimmed = line.trim();

        // The reference form is how a colour is declared now: the names are
        // aliases into the palette in force, so a theme is one assignment
        // rather than 437 edits. Leaving it out of this list does not fail -
        // it silently stops covering every colour in the design system, which
        // is the shape of hole this whole file exists to refuse.
        for (const auto* form : { "inline const juce::Colour& ", "inline const juce::Colour ",
                                  "inline constexpr int ", "inline constexpr float " })
            if (trimmed.startsWith (form))
                declared.addIfNotAlreadyThere (
                    trimmed.fromFirstOccurrenceOf (form, false, false)
                        .initialSectionContainingOnly ("abcdefghijklmnopqrstuvwxyz"
                                                       "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_"));
    }

    // Control case: a gate over an empty list is not a gate.
    REQUIRE (declared.size() > 60);
    REQUIRE (declared.contains ("controlHeight"));
    REQUIRE (declared.contains ("channelRamp"));

    // A colour declared the reference way, so this gate cannot go back to
    // covering the size ladder and the ramp while quietly ignoring the palette.
    REQUIRE (declared.contains ("accent"));
    REQUIRE (declared.contains ("funcTone"));

    // Declared but not yet quoted. This list may only ever get SHORTER: both of
    // these are animation durations, and the animator that consumes them lands
    // with the motion stage. Anything else appearing here is a token that was
    // added on speculation.
    const juce::StringArray awaitingTheMotionStage { "quickMs", "selectMs" };

    juce::String everythingElse;

    for (const auto* directory :
         { DEW_SOURCE_DIR, DEW_SOURCE_DIR "/../tests", DEW_SOURCE_DIR "/../tools" })
        for (const auto& entry :
             juce::RangedDirectoryIterator (juce::File (directory), true, "*.cpp;*.h"))
            // Tokens.cpp is excluded alongside Tokens.h: it holds the two
            // palettes, written as designated initialisers, so every token
            // name appears there as `.accent =` and would satisfy the search
            // below without the application referring to it at all.
            if (const auto name = entry.getFile().getFileName();
                name != "Tokens.h" && name != "Tokens.cpp")
                everythingElse += entry.getFile().loadFileAsString();

    juce::StringArray unused;

    for (const auto& name : declared)
    {
        // Whole word: `knob` must not be satisfied by `knobRow`.
        auto found = false;

        for (int i = everythingElse.indexOf (name); i >= 0;
             i = everythingElse.indexOf (i + 1, name))
        {
            const auto isWordCharacter = [] (juce::juce_wchar c)
            { return juce::CharacterFunctions::isLetterOrDigit (c) || c == '_'; };

            const juce::juce_wchar before = i > 0 ? everythingElse[i - 1] : ' ';
            const juce::juce_wchar after = everythingElse[i + name.length()];

            if (! isWordCharacter (before) && ! isWordCharacter (after))
            {
                found = true;
                break;
            }
        }

        if (! found && ! awaitingTheMotionStage.contains (name))
            unused.add (name);
    }

    INFO ("tokens nothing refers to:\n" << unused.joinIntoString ("\n"));
    CHECK (unused.isEmpty());

    // And the waiting list can only shrink.
    CHECK (awaitingTheMotionStage.size() <= 2);
}

TEST_CASE ("no view reads the wheel or the drag scale for itself", "[build][gate][gesture]")
{
    // Three wheel speeds - 4, 6 and 8 steps a notch - and one view of three
    // reading isReversed, which JUCE reports rather than applies. The two that
    // ignored it scrolled backwards for anyone running the Mac default, and
    // nobody noticed because each view was right about itself.
    //
    // Gestures.h is where the reading happens, so it is the one place allowed
    // to touch the raw fields.
    //
    // The two primitives that APPLY the drag scale are exempt as well: a knob
    // and a number field each hand JUCE the number Gestures.h names, which is
    // the opposite of deciding one. DewKnob.cpp joined the list when the knob
    // left DewControls.cpp - a name-based exemption is a list a moved file
    // silently falls off, and this gate went red for exactly that reason.
    const auto found = offenders (
        [] (const juce::String& line)
        {
            const auto trimmed = line.trim();

            if (trimmed.startsWith ("//") || trimmed.startsWith ("*") || trimmed.startsWith ("/*"))
                return false;

            return line.contains ("wheel.deltaX") || line.contains ("wheel.deltaY")
                   || line.contains ("wheel.isReversed")
                   || line.contains ("setMouseDragSensitivity");
        },
        { "Gestures.h", "DewControls.cpp", "DewKnob.cpp" });

    INFO ("views reading the wheel or the drag scale directly:\n" << found.joinIntoString ("\n"));
    CHECK (found.isEmpty());
}

TEST_CASE ("no editor writes an undoable property by hand", "[build][gate][undo]")
{
    // ProjectEdits had no scalar setter at all, so all twenty-seven property
    // writes in src/ui went straight to ValueTree and each re-implemented the
    // transaction rule around it - copy-pasted five times, and MISSING from a
    // sixth. The consequence was invisible in the file that had the bug and
    // obvious only across all six: dragging an audio channel's fade made one
    // undo step per frame.
    //
    // A write with no UndoManager is not caught, and deliberately: writing to a
    // detached copy nobody can undo is a different thing, and it says so where
    // it happens.
    //
    // The exemption is the DIRECTORY model/edits, not the files in it. It used
    // to name ProjectEdits.cpp, and when that file became seven the list would
    // have had to name all seven - which is the shape SourceScan.h warns about
    // above offenders(): a list of five that the sixth silently escapes. An
    // eighth edit file is exempt by being where the edits are.
    const auto found = offenders (
        [] (const juce::String& line)
        {
            const auto trimmed = line.trim();

            if (trimmed.startsWith ("//") || trimmed.startsWith ("*") || trimmed.startsWith ("/*"))
                return false;

            if (! line.contains (".setProperty (") || line.contains ("ProjectEdits::setProperty"))
                return false;

            return line.contains ("&undo") || line.contains ("getUndoManager()");
        },
        { "edits", "ProjectFactory.cpp", "ProjectSchema.cpp" });

    INFO ("undoable property writes outside ProjectEdits:\n" << found.joinIntoString ("\n"));
    CHECK (found.isEmpty());
}

TEST_CASE ("no source binds a key outside the hotkey registry", "[build][gate][hotkeys]")
{
    // There used to be two key tables that could not see each other: fifteen
    // addDefaultKeypress calls written inline in DewApplication::getCommandInfo,
    // and an if-chain in Gestures.cpp for the timeline views. Neither was wrong
    // about itself, and between them cmd-1 was swallowed by whichever view had
    // focus and bare `r` meant two different things.
    //
    // Hotkeys.cpp is where a binding is spelled. ScoreEditorComponent.cpp is
    // the one exemption: while its completion popup is open it owns Up, Down,
    // Return, Tab and Escape, and that is a modal handler rather than a
    // binding - nothing outside that popup can reach those keys, so putting
    // them in a table shared with the menu bar would say something untrue.
    const auto found = offenders (
        [] (const juce::String& line)
        {
            const auto trimmed = line.trim();

            if (trimmed.startsWith ("//") || trimmed.startsWith ("*") || trimmed.startsWith ("/*"))
                return false;

            return line.contains ("addDefaultKeypress (") || line.contains ("juce::KeyPress (")
                   || line.contains ("KeyPress::createFromDescription");
        },
        { "Hotkeys.h", "Hotkeys.cpp", "ScoreEditorComponent.cpp" });

    INFO ("keys bound outside the registry:\n" << found.joinIntoString ("\n"));
    CHECK (found.isEmpty());
}
