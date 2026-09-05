// =============================================================================
// The Rendering page: what every render starts as.
//
// The same class family as PreferencesPages.cpp, a second translation unit
// beside it. Split for the size gate, and along the seam that was already
// there - these five controls are the only ones on this window that write to
// Settings directly rather than invoking a command.
// =============================================================================

#include "ui/PreferencesPages.h"

#include <iterator>

#include "i18n/Strings.h"
#include "ui/RenderChoices.h"
#include "ui/design/Tokens.h"

namespace dew
{

using namespace tokens;

RenderingPage::RenderingPage (Settings& s)
    : settings (s)
{
    setComponentID ("preferencesRendering");

    for (const auto format : renderChoices::formats())
    {
        const auto id = (int) format + 1;
        formatBox.addItem (OfflineRenderer::nameFor (format), id);

        // Shown and disabled rather than left out. The render dialog explains
        // WHY under its controls; a preferences page that hid the row would
        // only raise the question of where MP3 went.
        if (! OfflineRenderer::isAvailable (format))
            formatBox.setItemEnabled (id, false);
    }

    renderChoices::fillDepths (depthBox);

    tailField.setRange (0.0, 30.0, 0.1);
    tailField.setNumDecimalPlaces (1);
    tailField.setSuffix (tr (StringId::unit_seconds));

    // Every value validated on read by Settings, so what arrives here is
    // already inside the range each control offers.
    const auto storedFormat = (RenderFormat) juce::jlimit (0, (int) RenderFormat::midi,
                                                           settings.getRenderFormat());

    if (OfflineRenderer::isAvailable (storedFormat))
        formatBox.setSelectedId ((int) storedFormat + 1, juce::dontSendNotification);

    // After the format, because the rate list is the format's.
    renderChoices::fillRates (rateBox, storedFormat);

    if (renderChoices::isOfferedRate (settings.getRenderSampleRate()))
        rateBox.setSelectedId (renderChoices::rateIdBase + settings.getRenderSampleRate(),
                               juce::dontSendNotification);

    if (renderChoices::isOfferedDepth (settings.getRenderBitDepth()))
        depthBox.setSelectedId (settings.getRenderBitDepth(), juce::dontSendNotification);

    tailField.setValue (settings.getRenderTailSeconds(), juce::dontSendNotification);
    normalizeToggle.setToggleState (settings.getRenderNormalize(), juce::dontSendNotification);

    formatBox.onChange = [this]
    {
        // The rate list narrows for mp3, so a rate that format cannot take must
        // not survive a switch to it - the same rule the render dialog follows,
        // through the same call.
        renderChoices::fillRates (rateBox, (RenderFormat) (formatBox.getSelectedId() - 1));
        store();
    };

    rateBox.onChange = [this] { store(); };
    depthBox.onChange = [this] { store(); };
    tailField.onValueChange = [this] { store(); };
    normalizeToggle.onClick = [this] { store(); };

    buildRows();
}

void RenderingPage::store()
{
    if (formatBox.getSelectedId() > 0)
        settings.setRenderFormat (formatBox.getSelectedId() - 1);

    if (rateBox.getSelectedId() > 0)
        settings.setRenderSampleRate (rateBox.getSelectedId() - renderChoices::rateIdBase);

    if (depthBox.getSelectedId() > 0)
        settings.setRenderBitDepth (depthBox.getSelectedId());

    settings.setRenderTailSeconds (tailField.getValue());
    settings.setRenderNormalize (normalizeToggle.getToggleState());
}

void RenderingPage::buildRows()
{
    juce::Component* controls[] { &formatBox, &rateBox, &depthBox, &tailField, &normalizeToggle };
    auto next = 0;

    for (const auto& entry : prefs::entries())
    {
        if (entry.page != prefs::Page::rendering)
            continue;

        jassert (next < (int) std::size (controls));

        auto* control = controls[next++];
        control->setWantsKeyboardFocus (true);

        // Three kinds of control and three setTooltip overrides, each of which
        // sets the accessible name as well. Component::setTooltip is not one of
        // them, so the type has to be recovered rather than assumed.
        if (auto* box = dynamic_cast<DewDropdown*> (control))
            box->setTooltip (tr (entry.description));
        else if (auto* field = dynamic_cast<DewNumberField*> (control))
            field->setTooltip (tr (entry.description));
        else if (auto* toggle = dynamic_cast<DewCheckbox*> (control))
        {
            // DewCheckbox has no setTooltip override, because juce::Button
            // names itself from its button text - and this one has none, since
            // the row beside it is already the label. So both are said here.
            toggle->setTooltip (tr (entry.description));
            toggle->setTitle (tr (entry.title));
        }

        // The row parents it - see PreferencesRow's constructor.
        auto row = std::make_unique<PreferencesRow> (entry, *control);
        addAndMakeVisible (*row);
        rows.push_back (std::move (row));
    }
}

void RenderingPage::resized()
{
    auto area = getLocalBounds();

    for (const auto& row : rows)
    {
        row->setBounds (area.removeFromTop (PreferencesRow::height()));
        area.removeFromTop (space::lg);
    }
}

void RenderingPage::reveal (const prefs::Entry* entry)
{
    for (const auto& row : rows)
        row->setRevealed (entry != nullptr && &row->getEntry() == entry);
}

int RenderingPage::getRequiredHeight() const
{
    return (int) rows.size() * (PreferencesRow::height() + space::lg);
}

} // namespace dew
