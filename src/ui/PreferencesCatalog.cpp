#include "ui/PreferencesCatalog.h"

#include "ui/Hotkeys.h"

namespace dew::prefs
{

namespace
{

/** Does `haystack` contain `needle`, already lowercased? */
bool contains (const juce::String& haystack, const juce::String& needle)
{
    return haystack.toLowerCase().contains (needle);
}

bool matches (const Entry& entry, const juce::String& lowercaseQuery)
{
    if (contains (tr (entry.title), lowercaseQuery)
        || contains (tr (entry.description), lowercaseQuery)
        || contains (tr (titleOf (entry.page)), lowercaseQuery))
        return true;

    for (const auto choice : entry.choices)
        if (contains (tr (choice), lowercaseQuery))
            return true;

    return false;
}

} // namespace

StringId titleOf (Page page)
{
    // No default, so -Wswitch-enum - an error under the ci preset - refuses a
    // page nobody has named. A page with no title would show as a blank row in
    // the sidebar and be reachable only by search.
    switch (page)
    {
        case Page::appearance: return StringId::preferences_page_appearance;
        case Page::audio: return StringId::preferences_page_audio;
        case Page::midi: return StringId::preferences_page_midi;
        case Page::rendering: return StringId::preferences_page_rendering;
        case Page::connections: return StringId::preferences_page_connections;
    }

    return StringId::preferences_page_appearance;
}

const std::vector<Page>& pageOrder()
{
    // Appearance first because it is what most people open this for, and the
    // three device pages together because they are one subject. Rendering sits
    // between them and Connections rather than at the end: it is a thing you
    // configure, and Connections is a thing you switch on.
    static const std::vector<Page> order {
        Page::appearance, Page::audio, Page::midi, Page::rendering, Page::connections,
    };

    return order;
}

// clang-format off
const std::vector<Entry>& entries()
{
    // A function-local static for the reason hotkeys::application() is one: the
    // StringId values are fine as constants, but keeping the two tables the
    // same shape is worth more than the microsecond.
    //
    // In sidebar order, so this file reads as the window does.
    static const std::vector<Entry> table
    {
        { Page::appearance, StringId::preferences_theme_title,
          StringId::preferences_theme_description,
          { StringId::command_viewThemeFirst_name,
            StringId::command_viewThemeHighContrast_name },
          CommandIDs::viewThemeFirst },
        { Page::appearance, StringId::preferences_uiScale_title,
          StringId::preferences_uiScale_description,
          { StringId::command_viewUiScaleFirst_name, StringId::command_viewUiScale125_name,
            StringId::command_viewUiScale150_name, StringId::command_viewUiScale175_name },
          CommandIDs::viewUiScaleFirst },
        { Page::appearance, StringId::preferences_motion_title,
          StringId::preferences_motion_description,
          { StringId::command_viewMotionFirst_name, StringId::command_viewMotionFull_name,
            StringId::command_viewMotionReduced_name },
          CommandIDs::viewMotionFirst },
        { Page::appearance, StringId::preferences_language_title,
          StringId::preferences_language_description,
          { StringId::menu_languageSystem }, 0 },

        // The three hosted pages. No command and no control of their own: an
        // entry here is how a person FINDS the panel, and the panel is what
        // they then use.
        { Page::audio, StringId::preferences_audio_device_title,
          StringId::preferences_audio_device_description, {}, CommandIDs::audioSettings },
        { Page::audio, StringId::preferences_audio_rate_title,
          StringId::preferences_audio_rate_description, {}, CommandIDs::audioSettings },

        { Page::midi, StringId::preferences_midi_devices_title,
          StringId::preferences_midi_devices_description, {}, CommandIDs::midiSettings },
        { Page::midi, StringId::preferences_midi_channel_title,
          StringId::preferences_midi_channel_description, {}, CommandIDs::midiSettings },

        { Page::rendering, StringId::preferences_render_format_title,
          StringId::preferences_render_format_description, {}, 0 },
        { Page::rendering, StringId::preferences_render_rate_title,
          StringId::preferences_render_rate_description, {}, 0 },
        { Page::rendering, StringId::preferences_render_depth_title,
          StringId::preferences_render_depth_description,
          { StringId::render_depth_bits16, StringId::render_depth_bits24,
            StringId::render_depth_float32 },
          0 },
        { Page::rendering, StringId::preferences_render_tail_title,
          StringId::preferences_render_tail_description, {}, 0 },
        { Page::rendering, StringId::preferences_render_normalize_title,
          StringId::preferences_render_normalize_description, {}, 0 },

        { Page::connections, StringId::preferences_connections_enable_title,
          StringId::preferences_connections_enable_description, {}, CommandIDs::mcpSettings },
        { Page::connections, StringId::preferences_connections_grants_title,
          StringId::preferences_connections_grants_description, {}, CommandIDs::mcpSettings },
    };

    return table;
}

// clang-format on
std::vector<const Entry*> search (const juce::String& query)
{
    const auto wanted = query.trim().toLowerCase();

    std::vector<const Entry*> found;

    for (const auto& entry : entries())
        if (wanted.isEmpty() || matches (entry, wanted))
            found.push_back (&entry);

    return found;
}

bool pageMatches (Page page, const juce::String& query)
{
    const auto wanted = query.trim().toLowerCase();

    if (wanted.isEmpty())
        return true;

    for (const auto& entry : entries())
        if (entry.page == page && matches (entry, wanted))
            return true;

    return false;
}

} // namespace dew::prefs
