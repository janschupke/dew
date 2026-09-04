#include "lang/Model.h"

namespace dew::lang
{

const ChannelSpec* Model::channel (std::string_view name) const noexcept
{
    for (const auto& item : channels)
        if (item.name == name)
            return &item;

    return nullptr;
}

const VoicingSpec* Model::voicing (std::string_view name) const noexcept
{
    for (const auto& item : voicings)
        if (item.name == name)
            return &item;

    return nullptr;
}

const RhythmSpec* Model::rhythm (std::string_view name) const noexcept
{
    for (const auto& item : rhythms)
        if (item.name == name)
            return &item;

    return nullptr;
}

const HarmonySpec* Model::harmony (std::string_view name) const noexcept
{
    for (const auto& item : harmonies)
        if (item.name == name)
            return &item;

    return nullptr;
}

const SectionSpec* Model::section (std::string_view name) const noexcept
{
    for (const auto& item : sections)
        if (item.name == name)
            return &item;

    return nullptr;
}
} // namespace dew::lang
