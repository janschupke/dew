#include <catch2/catch_test_macros.hpp>

#include "SourceScan.h"
#include "ui/design/Tokens.h"

using namespace dew::testing;
using namespace dew;

// =============================================================================
// The design-system gates.
//
// Tokens.h is exempt from most of these, because it is where the values are
// declared; a gate that forbade its own definitions would only be forbidding
// the design system from existing. Which of them it really needs is checked
// rather than assumed - see the exemption accounting in SourceScan.h.
// =============================================================================

TEST_CASE ("no source names a mouse cursor outside the vocabulary", "[build][gate][design]")
{
    // Nine call sites picked a juce::MouseCursor by hand, and two of them
    // disagreed about what a draggable thing looks like: the effect chain's
    // grip said DraggingHand and the playlist's clips said nothing at all, so
    // the gesture a person uses most had no feedback while a rarer one did.
    //
    // Cursors.h names them for the GESTURE - clickable, value, move, resizeX,
    // nib, idle - the same way the colours are named for their role.
    const auto found = offenders ([] (const juce::String& line)
                                  { return line.contains ("juce::MouseCursor::"); },
                                  { "Cursors.h" });

    INFO ("cursors chosen outside the vocabulary:\n" << found.joinIntoString ("\n"));
    CHECK (found.isEmpty());
}

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
        { return line.contains ("juce::Colour (0x") || line.contains ("juce::Colour(0x"); },
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
