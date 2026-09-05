#pragma once

#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "i18n/Strings.h"

namespace dew::prefs
{

/** The pages, in the order the sidebar lists them.

    Two kinds, and the difference is what builds them rather than what they
    hold. `appearance` and `rendering` are laid out FROM this catalog, a row per
    entry. `audio`, `midi` and `connections` embed the panel that already
    existed - AudioSettingsPanel, MidiSettingsPanel, McpConnectionsPanel - whole
    and unchanged, because those three are the settings, and a second copy of a
    device chooser is a second thing to keep in step with the device.
*/
enum class Page
{
    appearance,
    audio,
    midi,
    rendering,
    connections
};

/** One searchable setting.

    A hosted page's entries NAME what its panel holds rather than mirroring the
    panel's own labels: they are what a person types looking for it, and the
    control they lead to is the panel's. So this is a search index and a page
    map, not a second declaration of the controls.
*/
struct Entry
{
    Page page;

    /** What the row is called, and what it is for. `description` is the
        tooltip, the accessible name and most of what search matches. */
    StringId title;
    StringId description;

    /** The value labels, which search also matches - so "high contrast" finds
        Theme even though the word appears in none of its own text.

        StringIds only, which is why the language row lists just "System": the
        rest of that picker is endonyms, built at runtime from what
        resources/i18n compiled in, and a catalogue key cannot name one.
    */
    std::vector<StringId> choices;

    /** The command that applies this, or 0 when the page owns the control.

        Theme, motion and interface size are all commands already, and going
        through the command manager is what keeps DewApplication::perform the
        only implementation of each - the menu and this window are two views of
        one action rather than two actions that have to agree.
    */
    juce::CommandID command = 0;
};

const std::vector<Entry>& entries();
const std::vector<Page>& pageOrder();
StringId titleOf (Page);

/** The entries whose title, description, value labels or page name contain
    `query`, case-insensitively and ignoring surrounding space.

    Matched against tr() output rather than against the keys, so it filters in
    whatever language dew is running in. An empty query matches everything,
    which is what makes "clear the box" and "never typed in it" the same state.
*/
std::vector<const Entry*> search (const juce::String& query);

/** Whether `page` holds anything `query` matches - what hides a category in the
    sidebar. */
bool pageMatches (Page page, const juce::String& query);

} // namespace dew::prefs
