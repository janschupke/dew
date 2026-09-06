#include <catch2/catch_test_macros.hpp>

#include <set>

#include <juce_core/juce_core.h>

#include "SourceScan.h"

using namespace dew::testing;

namespace
{

/** True when `line` hands a sink that a PERSON reads a string literal.

    Named sinks rather than "any literal", for the reason the automatable-
    parameter gate is scoped rather than general: dew's sources are full of
    literals that are not English - node types, file-format ids ("filter",
    "lowpass"), icon names, JSON keys, colour token names. A gate that flagged
    those would cry wolf, and a gate that cries wolf is one people turn off.

    The EMPTY literal is allowed, and that is not an oversight. A DewIconButton
    is constructed with {} and told what it is a line later, which is exactly
    the shape "a control's tooltip and its accessible name are the same
    sentence" exists to hold.

    Sinks are added here as each one is emptied across the tree, so this gate
    has never been red on the tree it guards - a gate landed before its subject
    is a gate somebody disables to get on with the extraction.
*/
/** `line` with every diagnostic CODE removed.

    "E401" is not a sentence. It is an identifier that happens to be spelled in
    quotes - stable, documented and greppable, and the same four characters in
    every language - and it is the FIRST argument of the call whose SECOND
    argument is the message. Without this the gate reports the code of every
    diagnostic in the tree and the only way to quieten it is a list of files,
    which is the ratchet the exemption rule exists to remove.

    Matched on the shape rather than on a list: a letter, three digits, nothing
    else between the quotes. No sentence dew says is four characters long.
*/
juce::String withoutDiagnosticCodes (const juce::String& line)
{
    juce::String kept;
    auto i = 0;

    while (i < line.length())
    {
        const auto quote = line.indexOfChar (i, '"');

        if (quote < 0 || quote + 5 >= line.length())
        {
            kept += line.substring (i);
            break;
        }

        const auto candidate = line.substring (quote, quote + 6);
        const auto isCode = (candidate[1] == 'E' || candidate[1] == 'W') && candidate[5] == '"'
                            && juce::CharacterFunctions::isDigit (candidate[2])
                            && juce::CharacterFunctions::isDigit (candidate[3])
                            && juce::CharacterFunctions::isDigit (candidate[4]);

        if (! isCode)
        {
            kept += line.substring (i, quote + 1);
            i = quote + 1;
            continue;
        }

        kept += line.substring (i, quote);
        i = quote + 6;
    }

    return kept;
}

/** `line` with a literal's WRAPPER removed.

    juce::String ("Score") is the same sentence as "Score", and wearing the
    wrapper is how the score editor's own heading survived the whole first
    extraction pass: the gate asked what setText's argument STARTED with, and
    what it started with was `juce::String`. Two places in the tree - this
    file's subject at src/ui/ScoreEditorComponent.cpp and i18n.md - said the
    gate looked through it long before any code here did.

    The closing paren is left where it is. Nothing downstream reads it, and
    removing it would mean matching parens in a predicate that is deliberately
    textual.
*/
juce::String withoutStringWrappers (const juce::String& line)
{
    auto kept = line;

    for (const auto* wrapper : { "juce::String (\"", "juce::String(\"", "String (\"",
                                 "juce::CharPointer_UTF8 (\"", "CharPointer_UTF8 (\"" })
        kept = kept.replace (wrapper, "\"");

    return kept;
}

bool showsALiteral (const juce::String& raw)
{
    const auto line = withoutStringWrappers (withoutDiagnosticCodes (withoutArgumentNames (raw)));

    // Sinks whose text is the FIRST argument.
    //
    // beginNewTransaction is deliberately NOT among them, and it is the one
    // that looks like it should be. Fifty-five of them name an undo step, and
    // an undo step's name is read by getUndoDescription, which nothing outside
    // the tests calls - dew's Edit menu shows the COMMAND's name, not the
    // transaction's. Translating a string nobody displays is paying a
    // translator for a key the orphan gate would then have to allow.
    for (const auto* sink : { "setTooltip (", "setButtonText (", "setText (", "setTitle (",
                              "setSuffix (", "setCaption (", "showMessage (", "notes.push_back (",
                              "helps.push_back (", "warnings.add (", "addSectionHeader (" })
    {
        if (! line.contains (sink))
            continue;

        const auto rest = line.fromFirstOccurrenceOf (sink, false, false).trimStart();

        if (rest.startsWith ("\"") && ! rest.startsWith ("\"\""))
            return true;

        // A TERNARY, which the prefix test above cannot see: what follows the
        // sink is a condition, and the two sentences are past it. Three
        // controls read their own state back that way - the transport's mode
        // button and the panel chevron - and the gate reported all three clean.
        if (rest.contains ("?") && rest.contains ("\""))
            return true;
    }

    // A tooltip handed to a CONSTRUCTOR, which the sinks above cannot see: they
    // ask what follows `setTooltip (`, and a control given its sentence at the
    // point it is declared never calls one. Twenty-nine did, so most of the
    // toolbar and the whole transport bar were English written into a header -
    // and the gate this file exists to be reported it all as clean.
    //
    // Any literal in the braces, not only the second argument: DewIconButton
    // takes a path then a sentence, DewLetterToggle a letter then a colour then
    // a sentence, and a LETTER is a literal that stays one. So the letter is
    // excluded by being one character rather than by counting arguments.
    // ZoomButtons covers VerticalZoomButtons by substring, and both take
    // nothing BUT sentences. DewCheckbox's first argument is the label beside
    // the box - a thing a person reads, unlike the others' tooltips, and four
    // of the render panel's five were still English long after every other word
    // in that panel had a key.
    for (const auto* control :
         { "DewIconButton", "DewLetterToggle", "DewButton", "DewCheckbox", "ZoomButtons" })
    {
        if (! line.contains (control))
            continue;

        const auto braces = line.fromFirstOccurrenceOf ("{", false, false);

        for (auto i = 0; i < braces.length(); ++i)
        {
            if (braces[i] != '"')
                continue;

            const auto text = braces.substring (i + 1).upToFirstOccurrenceOf ("\"", false, false);
            i += text.length() + 1;

            // A letter is a label, not a sentence: the channel rack's R names
            // the control, and the roll's "+12" is a number of semitones. Both
            // are the same in every language, so what is asked here is whether
            // the literal is WORDS - more than one character, and some of them
            // letters.
            if (text.length() > 1 && text.containsAnyOf ("abcdefghijklmnopqrstuvwxyz"))
                return true;
        }
    }

    // A menu item's text is its SECOND argument - addItem (id, "Rename") - so
    // asking what the call starts with finds nothing at all. A prefix test here
    // covered none of the thirty menu items in the tree while looking exactly
    // like one that did, which is the failure "ask a shape, not a spelling"
    // names.
    //
    // A diagnostic's message is its second argument too, and its label its
    // fourth. The score language reaches its own catalogue rather than tr() -
    // src/lang/MessageCatalog.h says why - but a sentence written into
    // src/lang/ is exactly as invisible to a translator as one written into
    // src/ui/, so the sinks belong in the same list. The code is a literal and
    // stays one: "E401" is not a sentence, it is an identifier that happens to
    // be spelled in quotes, and it is the first argument rather than the
    // second.
    // A dialog's title is its SECOND argument too, and it is the one sentence of
    // a dialog that is not inside the panel: four of the nine were still raw
    // English - "Render", "Audio Settings", "MIDI Settings", "Randomize" - long
    // after every word inside those same panels had been translated, because no
    // sink in this list could see them.
    for (const auto* sink :
         { "addItem (", "addSubMenu (", "drawText (", "drawFittedText (", "FileChooser> (",
           "FileBasedDocument (", "diagnostics.error (", "diagnostics.warning (",
           "diagnostics.add (", "related.push_back (", "dialog::launch (" })
    {
        if (! line.contains (sink))
            continue;

        auto rest = line.fromFirstOccurrenceOf (sink, false, false);

        // A FileChooser takes its title, then a folder, then a WILDCARD - and
        // "*.sf2;*.SF2" is a pattern the filesystem reads, not a sentence a
        // person does. Reading the whole call reported the wildcard of a
        // chooser whose title was already translated, so only the title is
        // asked about. It ends at the first comma outside any nesting.
        if (juce::String (sink) == "FileChooser> (")
        {
            auto depth = 0;

            for (int i = 0; i < rest.length(); ++i)
            {
                const auto c = rest[i];

                if (c == '(' || c == '{')
                    ++depth;
                else if (c == ')' || c == '}')
                    --depth;
                else if (c == ',' && depth == 0)
                {
                    rest = rest.substring (0, i);
                    break;
                }
            }
        }

        if (rest.containsChar ('"') && ! rest.contains ("\"\""))
            return true;
    }

    return false;
}

} // namespace

TEST_CASE ("the scanner reads a wrapped call as one statement", "[build][gate][i18n]")
{
    // The gate above is only as honest as this. `.clang-format` wraps at column
    // 100, so the longest arguments in the tree - which are exactly the
    // sentences a person reads - end up on a line of their own, and a
    // line-oriented predicate sees `setTooltip (` followed by nothing.
    //
    // Five strings were hiding in that shape when this was written, including
    // two the status bar shows and one the render panel does.
    const auto file = juce::File::createTempFile (".cpp");

    file.replaceWithText ("void f()\n"
                          "{\n"
                          "    toggle.setTooltip (\n"
                          "        \"Adds inaudible noise so truncation does not distort\");\n"
                          "    other.setComponentID (\"dither\");\n"
                          "}\n");

    const auto statements = statementsWithNumbersOf (file);
    file.deleteFile();

    // The wrapped call is ONE statement, numbered by the line it starts on.
    auto joined = juce::String();

    for (const auto& s : statements)
        if (s.text.contains ("setTooltip"))
            joined = s.text;

    INFO (joined);
    CHECK (joined.contains ("setTooltip ("));
    CHECK (joined.contains ("Adds inaudible noise"));
    CHECK (showsALiteral (joined));

    // And the statement AFTER it did not get swallowed into the same one: a
    // join that ran on would report every later literal against this sink.
    auto sawTheNextOne = false;

    for (const auto& s : statements)
        if (s.text.contains ("setComponentID") && ! s.text.contains ("setTooltip"))
            sawTheNextOne = true;

    CHECK (sawTheNextOne);
}

TEST_CASE ("a lambda argument does not become one enormous statement", "[build][gate][i18n]")
{
    // Joining on paren depth alone runs from `forEachStatement (` to the
    // closing paren of the lambda passed to it, which in the score resolver is
    // sixty lines - and every literal inside them is then reported against
    // whichever sink appeared first. A line ending in `{` opens a block, and a
    // wrapped argument list never does.
    const auto file = juce::File::createTempFile (".cpp");

    file.replaceWithText ("void f()\n"
                          "{\n"
                          "    forEach (spec, [&] (const Statement& s)\n"
                          "    {\n"
                          "        if (s.key == \"chords\")\n"
                          "            report (\"E229\");\n"
                          "    });\n"
                          "}\n");

    const auto statements = statementsWithNumbersOf (file);
    file.deleteFile();

    for (const auto& s : statements)
    {
        INFO (s.text);
        CHECK_FALSE ((s.text.contains ("forEach (") && s.text.contains ("E229")));
    }
}

TEST_CASE ("no source shows a person a string literal", "[build][gate][i18n]")
{
    // The whole point of the catalogue. A sentence written into a source file
    // is a sentence no translator will ever see, and nothing else in the build
    // would report it.
    //
    // JUCE's own answer - TRANS() over LocalisedStrings - was rejected because
    // it keys on the ENGLISH TEXT, so correcting a typo silently orphans every
    // translation of it. dew's keys are structural, so a typo is a one-word
    // edit to en.json and nothing downstream notices.
    // Over STATEMENTS, not lines: see statementsWithNumbersOf. The formatter
    // wraps the longest arguments in the tree, and the longest arguments are
    // the sentences a person reads.
    // DewGallery is not in the application. tools/shot_main.cpp is its only
    // caller - it renders the design system to a PNG for a human to look at -
    // so its captions are read by whoever is working on dew and by nobody else.
    // Translating them would put words in the catalogue that no user can reach,
    // which the orphan gate below would then have to be told to allow.
    const auto found = offenders (showsALiteral, { "ui/design/DewGallery.cpp" },
                                  statementsWithNumbersOf);

    INFO ("English written into the source:\n" << found.joinIntoString ("\n"));
    CHECK (found.isEmpty());

    // Control case: a scanner that cannot see the defect it was written for is
    // not a gate. Both lines below are the transport bar's tempo field, before
    // and after.
    CHECK (showsALiteral ("    tempoField.setTooltip (\"Tempo - drag up and down\");"));
    CHECK_FALSE (
        showsALiteral ("    tempoField.setTooltip (tr (StringId::transport_tempo_help));"));
    CHECK_FALSE (showsALiteral ("    icon.setTooltip (\"\");"));

    // Worn inside a juce::String, which is how the score editor's own heading
    // survived the first extraction pass.
    CHECK (showsALiteral ("    heading.setText (juce::String (\"Score\"), dontSendNotification);"));
    CHECK (showsALiteral ("    label.setText (juce::CharPointer_UTF8 (\"Score\"), n);"));
    CHECK_FALSE (showsALiteral ("    heading.setText (juce::String (score.name), n);"));

    // The ternary shape, before and after.
    CHECK (showsALiteral ("    b.setButtonText (song ? \"Song\" : \"Pattern\");"));
    CHECK_FALSE (showsALiteral ("    b.setButtonText (tr (song ? StringId::a : StringId::b));"));
    CHECK_FALSE (showsALiteral ("    button.setComponentID (\"mute\");"));

    // A tooltip handed to a constructor, before and after. The letter toggle's
    // R is its own name and stays a literal.
    CHECK (showsALiteral ("    DewIconButton stopButton { icons::stop(), \"Stop and rewind\" };"));
    CHECK_FALSE (showsALiteral (
        "    DewIconButton stopButton { icons::stop(), tr (StringId::transport_stop_help) };"));
    CHECK_FALSE (showsALiteral (
        "    DewLetterToggle armButton { \"R\", colour::recording, tr (StringId::x) };"));

    // A section heading names a run of rows and is read exactly as they are.
    // Its text is the FIRST argument, unlike addItem's, which is why it sits in
    // the other list - and why a prefix test finds it where one for addItem
    // would not.
    CHECK (showsALiteral ("    menu.addSectionHeader (\"Pads\");"));
    CHECK_FALSE (showsALiteral ("    menu.addSectionHeader (row.label);"));

    // And the menu-item shape, which the prefix test above cannot see.
    CHECK (showsALiteral ("    menu.addItem ((int) MenuItem::rename, \"Rename\");"));
    CHECK_FALSE (
        showsALiteral ("    menu.addItem ((int) MenuItem::rename, tr (StringId::mixerRename));"));

    // An argument name is an identifier spelled as a literal, and reporting one
    // would leave a list of files as the only way to quieten the gate.
    CHECK_FALSE (showsALiteral (
        "    box.addItem (tr (StringId::unitHertz, Args{}.with (\"value\", rate)), rate);"));

    // A diagnostic. Its code is an identifier and stays a literal; its message
    // and its label are sentences and must not be.
    CHECK (showsALiteral (
        "    diagnostics.error (\"E210\", \"a bar holds 1 to 16 beats\", statement.range);"));
    CHECK_FALSE (showsALiteral (
        "    diagnostics.error (\"E210\", diagnostics.text (Msg::x), statement.range);"));
    CHECK (showsALiteral ("    d.notes.push_back (\"rename one of them\");"));
    CHECK_FALSE (showsALiteral ("    d.notes.push_back (diagnostics.text (Msg::x));"));
    CHECK (showsALiteral ("    d.related.push_back ({ peek().range, \"the file ends\" });"));

    // A code on its own is not a sentence, and neither is a musical note.
    CHECK_FALSE (showsALiteral ("    diagnostics.error (\"E217\", diagnostics.text (m), r);"));
    CHECK_FALSE (
        showsALiteral ("    result.notes.push_back ({ onset.startStep, length, pitch });"));

    // And the menu-item shape, which the prefix test above cannot see.
    CHECK (showsALiteral ("    menu.addItem ((int) MenuItem::rename, \"Rename\");"));
    CHECK_FALSE (
        showsALiteral ("    menu.addItem ((int) MenuItem::rename, tr (StringId::mixerRename));"));
}

namespace
{

/** Every dotted key the catalogue declares, depth-first. */
void collectKeys (const juce::var& node, const juce::String& prefix, juce::StringArray& keys)
{
    auto* object = node.getDynamicObject();

    if (object == nullptr)
        return;

    for (const auto& property : object->getProperties())
    {
        const auto key = prefix.isEmpty() ? property.name.toString() : prefix + "." + property.name;

        if (property.value.getDynamicObject() != nullptr)
            collectKeys (property.value, key, keys);
        else
            keys.add (key);
    }
}

/** Every StringId or Msg the code names, collected once, under `marker`.

    Once, rather than a search of the whole tree per key: four hundred keys
    against a megabyte of concatenated source is four hundred passes over it,
    and the gate took eleven seconds doing that.

    Collecting the identifier WHOLE is also what makes the comparison safe.
    param.attack mangles to a PREFIX of param.attackScale, so a substring search
    would report the shorter key as used by every mention of the longer one -
    and would look exactly like a gate that covered everything while covering
    almost nothing.
*/
std::set<juce::String> namedIdentifiers (const juce::String& marker)
{
    std::set<juce::String> named;

    for (const auto* directory : { DEW_SOURCE_DIR, DEW_TESTS_DIR })
        for (const auto& entry :
             juce::RangedDirectoryIterator (juce::File { directory }, true, "*.cpp;*.h"))
            for (const auto& line : codeLinesOf (entry.getFile()))
                for (auto i = line.indexOf (marker); i >= 0; i = line.indexOf (i + 1, marker))
                {
                    auto end = i + marker.length();

                    while (end < line.length()
                           && (juce::CharacterFunctions::isLetterOrDigit (line[end])
                               || line[end] == '_'))
                        ++end;

                    named.insert (line.substring (i + marker.length(), end));
                }

    return named;
}

} // namespace

TEST_CASE ("every string the catalogue declares is one the app asks for", "[build][gate][i18n]")
{
    // The orphan-token gate's twin, and the same failure it was written for: a
    // key nobody references is a sentence a translator will be paid for and
    // nobody will ever read, and nothing reports it - the generator emits it,
    // the enum grows an enumerator, and the build is green.
    //
    // Only this direction needs a test. A StringId the app names and the
    // catalogue does not hold is already a compile error, because the enum IS
    // the catalogue.
    const juce::File catalogue { juce::String (DEW_I18N_DIR) + "/en.json" };
    REQUIRE (catalogue.existsAsFile());

    juce::StringArray keys;
    collectKeys (juce::JSON::parse (catalogue), {}, keys);

    // Control case: a gate over an empty list is not a gate.
    INFO ("keys declared: " << keys.size());
    REQUIRE (keys.size() > 300);
    REQUIRE (keys.contains ("param.cutoff.caption"));
    REQUIRE (keys.contains ("lang.generator.tooManyChannels.message"));

    // One file, two enums. The `lang.` subtree is generated into dew_lang as
    // Msg with the prefix stripped - it already lives in namespace dew::lang -
    // and everything else into dew_i18n as StringId. A translator sees one
    // catalogue; which of dew's libraries holds a sentence is a fact about the
    // link graph and no business of theirs. See cmake/GenStrings.cmake.
    //
    // Comments stripped by codeLinesOf, so a key mentioned in prose does not
    // count as used - ceum learned that one the expensive way, with a key
    // prefix named in a comment hiding a hundred and forty-one dead entries
    // from its own scanner.
    const auto stringIds = namedIdentifiers ("StringId::");
    const auto messages = namedIdentifiers ("Msg::");

    // Control case: a set that collected nothing would report every key unused,
    // which fails loudly - but one that collected the wrong thing would report
    // every key USED and pass in silence.
    INFO ("StringIds named in the source: " << stringIds.size());
    INFO ("Msgs named in the source: " << messages.size());
    REQUIRE (stringIds.size() > 300);
    REQUIRE (stringIds.count ("param_cutoff_caption") == 1);
    REQUIRE (stringIds.count ("param_attackScale_name") == 1);
    REQUIRE (messages.size() > 10);
    REQUIRE (messages.count ("generator_tooManyChannels_message") == 1);

    juce::StringArray unused;

    for (const auto& key : keys)
    {
        const auto isLanguage = key.startsWith ("lang.");
        const auto& named = isLanguage ? messages : stringIds;
        const auto identifier = (isLanguage ? key.fromFirstOccurrenceOf ("lang.", false, false)
                                            : key)
                                    .replaceCharacter ('.', '_');

        if (named.count (identifier) == 0)
            unused.add (key);
    }

    INFO ("declared in en.json and asked for nowhere:\n" << unused.joinIntoString ("\n"));
    CHECK (unused.isEmpty());
}
