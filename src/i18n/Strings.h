#pragma once

#include <juce_core/juce_core.h>

#include "i18n/Arg.h"
#include "i18n/StringIds.h"

namespace dew
{

/** The active locale's text for `id`.

    A REFERENCE, into a vector built once when the locale is chosen. Two
    consequences, both deliberate:

      - g.drawText (tr (StringId::playlistEmpty), ...) allocates nothing per
        paint. It is CHEAPER than the literal it replaces, because juce::String
        has no small-string optimisation and a const char* becomes a heap
        allocation every time it crosses into one.
      - The locale is chosen ONCE, at startup, and changing it needs a relaunch.
        A reference into a vector that a later setLocale replaced would dangle,
        and "takes effect next launch" is what the alternative - handing every
        caller a copy, forever - would be paying for.

    NEVER empty. A row the catalogue does not hold returns the key's own dotted
    path, which is visible in a dew_shot render and impossible to mistake for
    prose. Empty would silently satisfy "every control in the window says what
    it is" (HoverHelpTests) and "a control's tooltip and its accessible name are
    the same sentence" (AccessibilityTests) - two gates that read a tooltip and
    treat blank as silence. A fallback that returns "" turns both into no-ops.
*/
const juce::String& tr (StringId id);

/** The same, with its arguments substituted. See MessageFormat.h for the
    subset. */
juce::String tr (StringId id, const Args& arguments);

/** `id`'s text in a NAMED locale, by value, negotiated the same way.

    For the one thing that must not read the active locale: content written into
    a file. `dew_render --write-demos` and `--write-presets` produce artefacts
    that are committed and compared byte for byte, so what they write has to be
    a function of the code and never of what the machine running it was set to.
    Passing the tag makes that a property of the call rather than of the order
    the suite happened to run in.

    By value, not by reference, because there is no table to point into: nothing
    is cached per locale and nothing dangles. tr() is the one to reach for
    everywhere a person is reading.
*/
juce::String trIn (juce::StringRef locale, StringId id);
juce::String trIn (juce::StringRef locale, StringId id, const Args& arguments);

/** The tag every other locale falls back to, row by row - what the generator
    wrote first, and what a file-bound artefact is written in. */
juce::String referenceLocale();

/** Choose the locale, once, from a BCP-47 tag.

    Negotiated against what was compiled in: an exact tag wins, then the
    language alone (fr-CA falls back to fr), then the reference locale. Calling
    it after anything has held a tr() reference is a dangling reference, which
    is why nothing but DewApplication::initialise calls it.
*/
void setLocale (juce::StringRef tag);

/** The tag actually in force, which is what negotiation settled on rather than
    what was asked for. */
juce::String activeLocale();

/** Every tag compiled into this build, reference locale first. */
juce::StringArray availableLocales();

/** What a language calls ITSELF - "English", "Deutsch", "Cestina".

    Deliberately not a catalogue key, and this is the one string in dew that
    argues for staying out. A language picker shows every language in its own
    language, in every locale: a German reader looking for German looks for
    "Deutsch", not for whatever English calls it. So there is nothing here to
    translate, and a per-locale key would also be a key no data-driven menu
    could name - the orphan gate looks for StringId::x written down, and a loop
    over availableLocales() writes none.

    An unknown tag answers with the tag, which is wrong but visible.
*/
juce::String endonymOf (juce::StringRef tag);

/** The dotted key `id` was generated from - "transport.tempo.help".

    For a test's INFO, for the missing-row fallback, and for nothing else.
    Nothing resolves a string BY its path, because that would be a way to name
    a key the enum does not have.
*/
juce::String keyOf (StringId id);

/** The argument names `id`'s message asks for, recorded at generation time. */
juce::StringArray argumentNamesOf (StringId id);

} // namespace dew
