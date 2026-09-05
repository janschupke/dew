#include "ui/RenderChoices.h"

#include "i18n/Strings.h"

namespace dew::renderChoices
{

const juce::Array<RenderFormat>& formats()
{
    static const juce::Array<RenderFormat> table {
        RenderFormat::wav,
        RenderFormat::flac,
        RenderFormat::mp3,
        RenderFormat::midi,
    };

    return table;
}

const juce::Array<int>& allRates()
{
    static const juce::Array<int> rates { 44100, 48000, 88200, 96000 };
    return rates;
}

const juce::Array<int>& ratesFor (RenderFormat format)
{
    // mp3 takes neither of the two high rates, so a switch to it must not leave
    // a rate selected that the encoder would refuse.
    static const juce::Array<int> mp3 { 32000, 44100, 48000 };

    return format == RenderFormat::mp3 ? mp3 : allRates();
}

bool isOfferedRate (int hz)
{
    return allRates().contains (hz) || ratesFor (RenderFormat::mp3).contains (hz);
}

bool isOfferedDepth (int bits)
{
    return bits == 16 || bits == 24 || bits == 32;
}

void fillRates (juce::ComboBox& box, RenderFormat format)
{
    const auto& rates = ratesFor (format);
    const auto previous = box.getSelectedId() - rateIdBase;

    box.clear (juce::dontSendNotification);

    for (const auto rate : rates)
        box.addItem (tr (StringId::unit_hertzValue, Args {}.with ("value", rate)),
                     rateIdBase + rate);

    box.setSelectedId (rateIdBase + (rates.contains (previous) ? previous : 44100),
                       juce::dontSendNotification);
}

void fillDepths (juce::ComboBox& box)
{
    box.addItem (tr (StringId::render_depth_bits16), 16);
    box.addItem (tr (StringId::render_depth_bits24), 24);
    box.addItem (tr (StringId::render_depth_float32), 32);
}

} // namespace dew::renderChoices
