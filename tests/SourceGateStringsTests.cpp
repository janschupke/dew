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
/** `line` with every argument NAME removed.

    args ("value", rate) names the placeholder a message interpolates; it is an
    identifier that happens to be spelled as a literal, and no translator will
    ever see it. Without this the gate reports every formatted message as an
    offence and the only way to quieten it is a list of files - which is the
    ratchet the exemption rule exists to remove.
*/
juce::String withoutArgumentNames (const juce::String& line)
{
    juce::String kept;
    auto i = 0;

    while (i < line.length())
    {
        const auto next = line.indexOf (i, ".with (\"");

        if (next < 0)
        {
            kept += line.substring (i);
            break;
        }

        const auto nameStart = next + 8;
        const auto nameEnd = line.indexOfChar (nameStart, '"');

        kept += line.substring (i, nameStart - 1);
        i = nameEnd < 0 ? line.length() : nameEnd + 1;
    }

    return kept;
}

bool showsALiteral (const juce::String& raw)
{
    const auto line = withoutArgumentNames (raw);

    // Sinks whose text is the FIRST argument.
    //
    // beginNewTransaction is deliberately NOT among them, and it is the one
    // that looks like it should be. Fifty-five of them name an undo step, and
    // an undo step's name is read by getUndoDescription, which nothing outside
    // the tests calls - dew's Edit menu shows the COMMAND's name, not the
    // transaction's. Translating a string nobody displays is paying a
    // translator for a key the orphan gate would then have to allow.
    for (const auto* sink : { "setTooltip (", "setButtonText (", "setText (", "setTitle (",
                              "setSuffix (", "showMessage (" })
    {
        if (! line.contains (sink))
            continue;

        const auto rest = line.fromFirstOccurrenceOf (sink, false, false).trimStart();

        if (rest.startsWith ("\"") && ! rest.startsWith ("\"\""))
            return true;
    }

    // A menu item's text is its SECOND argument - addItem (id, "Rename") - so
    // asking what the call starts with finds nothing at all. A prefix test here
    // covered none of the thirty menu items in the tree while looking exactly
    // like one that did, which is the failure "ask a shape, not a spelling"
    // names.
    for (const auto* sink : { "addItem (", "addSubMenu (", "drawText (", "drawFittedText (" })
    {
        if (! line.contains (sink))
            continue;

        const auto rest = line.fromFirstOccurrenceOf (sink, false, false);

        if (rest.containsChar ('"') && ! rest.contains ("\"\""))
            return true;
    }

    return false;
}

} // namespace

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
    const auto found = offenders (showsALiteral);

    INFO ("English written into the source:\n" << found.joinIntoString ("\n"));
    CHECK (found.isEmpty());

    // Control case: a scanner that cannot see the defect it was written for is
    // not a gate. Both lines below are the transport bar's tempo field, before
    // and after.
    CHECK (showsALiteral ("    tempoField.setTooltip (\"Tempo - drag up and down\");"));
    CHECK_FALSE (
        showsALiteral ("    tempoField.setTooltip (tr (StringId::transport_tempo_help));"));
    CHECK_FALSE (showsALiteral ("    icon.setTooltip (\"\");"));
    CHECK_FALSE (showsALiteral ("    button.setComponentID (\"mute\");"));

    // And the menu-item shape, which the prefix test above cannot see.
    CHECK (showsALiteral ("    menu.addItem ((int) MenuItem::rename, \"Rename\");"));
    CHECK_FALSE (
        showsALiteral ("    menu.addItem ((int) MenuItem::rename, tr (StringId::mixerRename));"));

    // An argument name is an identifier spelled as a literal, and reporting one
    // would leave a list of files as the only way to quieten the gate.
    CHECK_FALSE (showsALiteral (
        "    box.addItem (tr (StringId::unitHertz, Args{}.with (\"value\", rate)), rate);"));

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

/** Every StringId the code names, collected once.

    Once, rather than a search of the whole tree per key: four hundred keys
    against a megabyte of concatenated source is four hundred passes over it,
    and the gate took eleven seconds doing that.

    Collecting the identifier WHOLE is also what makes the comparison safe.
    param.attack mangles to a PREFIX of param.attackScale, so a substring search
    would report the shorter key as used by every mention of the longer one -
    and would look exactly like a gate that covered everything while covering
    almost nothing.
*/
std::set<juce::String> namedStringIds()
{
    std::set<juce::String> named;
    const juce::String marker { "StringId::" };

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

    // Comments stripped by codeLinesOf, so a key mentioned in prose does not
    // count as used - ceum learned that one the expensive way, with a key
    // prefix named in a comment hiding a hundred and forty-one dead entries
    // from its own scanner.
    const auto named = namedStringIds();

    // Control case: a set that collected nothing would report every key unused,
    // which fails loudly - but one that collected the wrong thing would report
    // every key USED and pass in silence.
    INFO ("StringIds named in the source: " << named.size());
    REQUIRE (named.size() > 300);
    REQUIRE (named.count ("param_cutoff_caption") == 1);
    REQUIRE (named.count ("param_attackScale_name") == 1);

    juce::StringArray unused;

    for (const auto& key : keys)
        if (named.count (key.replaceCharacter ('.', '_')) == 0)
            unused.add (key);

    INFO ("declared in en.json and asked for nowhere:\n" << unused.joinIntoString ("\n"));
    CHECK (unused.isEmpty());
}
