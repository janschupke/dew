#include <catch2/catch_test_macros.hpp>

#include <algorithm>

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

    for (const auto* expected : { "Tokens.h", "DewControls.cpp", "PianoRollComponent.cpp",
                                  "AudioEngine.cpp", "ProjectSchema.cpp", "Settings.cpp",
                                  "EffectModules.cpp", "MainComponent.cpp" })
    {
        INFO ("expected a file named " << expected << " under DEW_SOURCE_DIR");
        REQUIRE (std::any_of (files.begin(), files.end(),
                              [expected] (const juce::File& f) { return f.getFileName() == expected; }));
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

    for (const auto& source : compiled)
    {
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

TEST_CASE ("every layer is represented in the scanned sources", "[build][gate]")
{
    // Named directories rather than a count, so moving one layer out cannot be
    // masked by another growing.
    for (const auto* layer : { "model", "engine", "io", "ui", "app" })
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
    const auto found = offenders ([] (const juce::String& line)
    {
        const auto trimmed = line.trim();

        if (! trimmed.startsWith ("#include"))
            return false;

        return trimmed.contains ("juce_audio_devices")
               || trimmed.contains ("juce_audio_formats")
               || trimmed.contains ("\"io/");
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
    juce::StringArray names;

    const auto collect = [&names] (const std::vector<dew::AutomationParamSpec>& specs)
    {
        for (const auto& spec : specs)
            if (spec.property != nullptr)
                names.addIfNotAlreadyThere (spec.property->toString());
    };

    collect (dew::channelParams());
    collect (dew::mixerTrackParams());
    collect (dew::masterParams());
    collect (dew::oscParams());

    for (const auto& descriptor : dew::effectDescriptors())
        collect (dew::effectParams (descriptor.id));

    // An effect's id and one of its parameters share a spelling in one case -
    // "drive" is both - and the id is a value a file legitimately contains.
    for (const auto& descriptor : dew::effectDescriptors())
        names.removeString (descriptor.id);

    // Control case: a gate over an empty list is not a gate.
    REQUIRE (names.size() > 15);
    REQUIRE (names.contains ("cutoff"));
    REQUIRE (names.contains ("midFreq"));

    const auto found = dew::testing::offenders ([&names] (const juce::String& line)
    {
        const auto trimmed = line.trim();

        // Doc comments name properties all through this codebase, deliberately.
        if (trimmed.startsWith ("//") || trimmed.startsWith ("*") || trimmed.startsWith ("/*"))
            return false;

        for (const auto& name : names)
            if (line.contains ("\"" + name + "\""))
                return true;

        return false;
    }, { "Ids.h" });

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
    const auto found = offenders ([] (const juce::String& line)
    {
        const auto trimmed = line.trim();

        if (trimmed.startsWith ("//") || trimmed.startsWith ("*") || trimmed.startsWith ("/*"))
            return false;

        return line.contains ("juce::Colour (0x") || line.contains ("juce::Colour(0x");
    }, { "Tokens.h" });

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
    const auto found = offenders ([] (const juce::String& line)
    {
        const auto trimmed = line.trim();

        if (trimmed.startsWith ("//") || trimmed.startsWith ("*") || trimmed.startsWith ("/*"))
            return false;

        for (const auto* call : { ".withAlpha (", ".brighter (", ".darker (",
                                  ".withSaturation (", ".withBrightness (",
                                  ".withMultipliedSaturation (", ".withMultipliedBrightness (" })
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

                    if (i > 0 && (juce::CharacterFunctions::isLetterOrDigit (argument[i - 1])
                                  || argument[i - 1] == '.' || argument[i - 1] == '_'))
                        continue;

                    const auto number = argument.substring (i).initialSectionContainingOnly ("0123456789.");

                    if (number != "0.0" && number != "1.0" && number != "0" && number != "1")
                        return true;
                }
            }
        }

        return false;
    }, { "Tokens.h" });

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
    const auto found = offenders ([] (const juce::String& line)
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
    }, { "Tokens.h", "Icons.cpp" });

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
    struct Rung { int value; const char* name; };

    const Rung ladder[] {
        { tokens::size::controlHeight,   "controlHeight" },
        { tokens::size::controlHeightSm, "controlHeightSm" },
        { tokens::size::iconButton,      "iconButton" },
        { tokens::size::knob,            "knob" },
        { tokens::size::rowHeight,       "rowHeight or stripToolbar" },
        { tokens::size::rulerHeight,     "rulerHeight or letterToggle" },
        { tokens::size::stripStatus,     "stripStatus or iconButton" },
        { tokens::size::stripHeading,    "stripHeading or controlHeight" },
        { tokens::size::stripFormRow,    "stripFormRow" },
        { tokens::size::stripTabs,       "stripTabs" },
        { tokens::size::stripTransport,  "stripTransport" },
        { tokens::size::gutterChannel,   "gutterChannel" },
        { tokens::size::gutterTrack,     "gutterTrack" },
        { tokens::size::gutterKeyboard,  "gutterKeyboard" },
        { tokens::size::gutterLabel,     "gutterLabel" },
        { tokens::size::scrollThickness, "scrollThickness or meterHeight" },
        { tokens::size::knobRow,         "knobRow" },
    };

    const auto found = offenders ([&ladder] (const juce::String& line)
    {
        const auto trimmed = line.trim();

        if (! trimmed.startsWith ("constexpr int") && ! trimmed.startsWith ("static constexpr int"))
            return false;

        const auto declaration = trimmed.fromFirstOccurrenceOf ("int ", false, false);
        const auto name = declaration.upToFirstOccurrenceOf ("=", false, false).trim();
        const auto value = declaration.fromFirstOccurrenceOf ("=", false, false)
                                      .upToFirstOccurrenceOf (";", false, false).trim();

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
    }, { "Tokens.h" });

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
    const auto found = offenders ([] (const juce::String& line)
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

                if (arguments.contains (",") && bareInteger (arguments.fromFirstOccurrenceOf (",", false, false)))
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
    }, { "Tokens.h" });

    INFO ("gaps and insets written as bare numbers:\n" << found.joinIntoString ("\n"));
    CHECK (found.isEmpty());
}

TEST_CASE ("no source picks its own refresh rate", "[build][gate][design]")
{
    // A widget that starts a 30Hz timer by literal is a widget that will not
    // follow when the application decides what "a list refresh" costs, and dew
    // had one - the transport bar - beside four others already quoting
    // motion::uiRefreshHz and motion::playheadHz.
    const auto found = offenders ([] (const juce::String& line)
    {
        const auto trimmed = line.trim();

        if (trimmed.startsWith ("//") || trimmed.startsWith ("*") || trimmed.startsWith ("/*"))
            return false;

        return trimmed.contains ("startTimerHz (")
               && juce::CharacterFunctions::isDigit (
                      trimmed.fromFirstOccurrenceOf ("startTimerHz (", false, false)[0]);
    }, { "Tokens.h" });

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

        for (const auto* form : { "inline const juce::Colour ", "inline constexpr int ",
                                  "inline constexpr float " })
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

    // Declared but not yet quoted. This list may only ever get SHORTER: both of
    // these are animation durations, and the animator that consumes them lands
    // with the motion stage. Anything else appearing here is a token that was
    // added on speculation.
    const juce::StringArray awaitingTheMotionStage { "quickMs", "selectMs" };

    juce::String everythingElse;

    for (const auto* directory : { DEW_SOURCE_DIR, DEW_SOURCE_DIR "/../tests", DEW_SOURCE_DIR "/../tools" })
        for (const auto& entry : juce::RangedDirectoryIterator (juce::File (directory), true, "*.cpp;*.h"))
            if (entry.getFile().getFileName() != "Tokens.h")
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
            {
                return juce::CharacterFunctions::isLetterOrDigit (c) || c == '_';
            };

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
    // Gestures.cpp is where the reading happens, so it is the one place allowed
    // to touch the raw fields.
    const auto found = offenders ([] (const juce::String& line)
    {
        const auto trimmed = line.trim();

        if (trimmed.startsWith ("//") || trimmed.startsWith ("*") || trimmed.startsWith ("/*"))
            return false;

        return line.contains ("wheel.deltaX") || line.contains ("wheel.deltaY")
               || line.contains ("wheel.isReversed")
               || line.contains ("setMouseDragSensitivity");
    }, { "Gestures.h", "Gestures.cpp", "DewControls.cpp" });

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
    const auto found = offenders ([] (const juce::String& line)
    {
        const auto trimmed = line.trim();

        if (trimmed.startsWith ("//") || trimmed.startsWith ("*") || trimmed.startsWith ("/*"))
            return false;

        if (! line.contains (".setProperty (") || line.contains ("ProjectEdits::setProperty"))
            return false;

        return line.contains ("&undo") || line.contains ("getUndoManager()");
    }, { "ProjectEdits.cpp", "ProjectFactory.cpp", "ProjectSchema.cpp" });

    INFO ("undoable property writes outside ProjectEdits:\n" << found.joinIntoString ("\n"));
    CHECK (found.isEmpty());
}
