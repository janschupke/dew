#include "DewGallery.h"

#include "Icons.h"
#include "Tokens.h"

namespace dew
{

using namespace tokens;

namespace
{

struct Swatch { const char* name; juce::Colour value; };

std::vector<Swatch> palette()
{
    return {
        { "wellDeep", colour::wellDeep }, { "well", colour::well },
        { "background", colour::background }, { "surface", colour::surface },
        { "surfaceRaised", colour::surfaceRaised }, { "surfaceHover", colour::surfaceHover },
        { "divider", colour::divider }, { "dividerStrong", colour::dividerStrong },
        { "outline", colour::outline },
        { "textPrimary", colour::textPrimary }, { "textSecondary", colour::textSecondary },
        { "textDisabled", colour::textDisabled },
        { "accent", colour::accent }, { "accentMuted", colour::accentMuted },
        { "playhead", colour::playhead }, { "recording", colour::recording },
        { "success", colour::success }, { "warning", colour::warning },
        { "danger", colour::danger },
        { "beatShade", colour::beatShade }, { "barShade", colour::barShade },
    };
}

} // namespace

DewGallery::DewGallery()
{
    setComponentID ("gallery");

    const auto add = [this] (juce::Component* c) { controls.add (c); addAndMakeVisible (c); return c; };

    // --- buttons in every role and state -------------------------------------
    add (new DewButton ("Normal", DewButton::Role::normal));

    auto* primary = new DewButton ("Primary", DewButton::Role::primary);
    add (primary);

    auto* ghost = new DewButton ("Ghost", DewButton::Role::ghost);
    add (ghost);

    auto* danger = new DewButton ("Danger", DewButton::Role::danger);
    add (danger);

    auto* toggled = new DewButton ("Toggled", DewButton::Role::normal);
    toggled->setClickingTogglesState (true);
    toggled->setToggleState (true, juce::dontSendNotification);
    add (toggled);

    auto* disabled = new DewButton ("Disabled", DewButton::Role::normal);
    disabled->setEnabled (false);
    add (disabled);

    // --- icon buttons and letter toggles -------------------------------------
    add (new DewIconButton (icons::play(), "Play"));
    add (new DewIconButton (icons::stop(), "Stop"));

    auto* looping = new DewIconButton (icons::loop(), "Loop");
    looping->setClickingTogglesState (true);
    looping->setToggleState (true, juce::dontSendNotification);
    add (looping);

    add (new DewIconButton (icons::plus(), "Add"));
    add (new DewIconButton (icons::trash(), "Delete"));

    auto* muted = new DewLetterToggle ("M", colour::warning, "Mute");
    muted->setToggleState (true, juce::dontSendNotification);
    add (muted);

    add (new DewLetterToggle ("S", colour::success, "Solo"));

    // --- knobs ---------------------------------------------------------------
    auto* cutoff = new DewKnob ("CUTOFF", 20.0, 20000.0, 1.0);
    cutoff->setValue (2400.0, juce::dontSendNotification);
    cutoff->setNumDecimalPlaces (0);
    add (cutoff);

    auto* resonance = new DewKnob ("RES", 0.0, 1.0, 0.001);
    resonance->setValue (0.35, juce::dontSendNotification);
    add (resonance);

    auto* pan = new DewKnob ("PAN", -1.0, 1.0, 0.001);
    pan->setValue (-0.4, juce::dontSendNotification);
    pan->setBipolar (true);
    add (pan);

    // --- number fields -------------------------------------------------------
    auto* tempo = new DewNumberField();
    tempo->setRange (20.0, 300.0, 0.1);
    tempo->setValue (128.0, juce::dontSendNotification);
    tempo->setNumDecimalPlaces (1);
    tempo->setSuffix (" bpm");
    add (tempo);

    auto* steps = new DewNumberField();
    steps->setRange (1.0, 256.0, 1.0);
    steps->setValue (32.0, juce::dontSendNotification);
    steps->setNumDecimalPlaces (0);
    steps->setCaption ("STEPS");
    add (steps);

    auto* octave = new DewNumberField();
    octave->setRange (-4.0, 4.0, 1.0);
    octave->setValue (-1.0, juce::dontSendNotification);
    octave->setNumDecimalPlaces (0);
    octave->setCaption ("OCT");
    add (octave);

    // --- dropdowns -----------------------------------------------------------
    // Stock JUCE widgets styled by DewLookAndFeel rather than dew primitives,
    // so this is where a change to that styling gets seen.
    auto* wave = new juce::ComboBox();
    wave->addItemList ({ "Sine", "Saw", "Square", "Triangle" }, 1);
    wave->setSelectedId (2, juce::dontSendNotification);
    add (wave);

    auto* routing = new juce::ComboBox();
    routing->addItemList ({ "Insert 1", "Insert 2", "Insert 3", "Insert 4" }, 1);
    routing->setSelectedId (1, juce::dontSendNotification);
    add (routing);

    auto* unavailable = new juce::ComboBox();
    unavailable->addItem ("Unavailable", 1);
    unavailable->setSelectedId (1, juce::dontSendNotification);
    unavailable->setEnabled (false);
    add (unavailable);
}

DewGallery::~DewGallery() = default;

int DewGallery::getRequiredHeight()
{
    // Measured against an unbounded height. Deriving it from the current height
    // would mean a gallery that is too short can never report that it is.
    return layOut ({ 0, 0, juce::jmax (600, getWidth()), 100000 }, false) + space::xxl;
}

void DewGallery::resized()
{
    layOut (getLocalBounds().reduced (space::xxl), true);
}

int DewGallery::layOut (juce::Rectangle<int> area, bool apply)
{
    if (apply)
        sections.clear();


    // The page title is painted in this strip; sections start below it.
    area.removeFromTop (44);

    int index = 0;

    const auto place = [&] (juce::Component* c, juce::Rectangle<int> bounds)
    {
        if (apply)
            c->setBounds (bounds);
    };

    const auto sectionHeading = [&] (const juce::String& title, int height)
    {
        area.removeFromTop (space::xl);
        auto heading = area.removeFromTop (22);

        if (apply)
            sections.add ({ title, heading });

        area.removeFromTop (space::sm);
        return area.removeFromTop (height);
    };

    // Buttons.
    {
        auto row = sectionHeading ("Buttons", size::controlHeight);

        for (int i = 0; i < 6; ++i)
        {
            place (controls[index++], row.removeFromLeft (104));
            row.removeFromLeft (space::md);
        }
    }

    // Icon buttons.
    {
        auto row = sectionHeading ("Icon buttons and toggles", size::iconButton + 6);

        for (int i = 0; i < 7; ++i)
        {
            place (controls[index++], row.removeFromLeft (size::iconButton + 6)
                                          .withHeight (size::iconButton + 6));
            row.removeFromLeft (space::md);
        }
    }

    // Knobs.
    {
        auto row = sectionHeading ("Knobs", 78);

        for (int i = 0; i < 3; ++i)
        {
            place (controls[index++], row.removeFromLeft (72));
            row.removeFromLeft (space::lg);
        }
    }

    // Number fields.
    {
        auto row = sectionHeading ("Number fields - drag up and down to change", 40);

        place (controls[index++], row.removeFromLeft (120).withHeight (size::controlHeight));
        row.removeFromLeft (space::md);
        place (controls[index++], row.removeFromLeft (86).withHeight (40));
        row.removeFromLeft (space::md);
        place (controls[index++], row.removeFromLeft (86).withHeight (40));
    }

    // Dropdowns.
    {
        auto row = sectionHeading ("Dropdowns", size::controlHeight);

        for (int i = 0; i < 3; ++i)
        {
            place (controls[index++], row.removeFromLeft (150).withHeight (size::controlHeight));
            row.removeFromLeft (space::md);
        }
    }

    // Icons and palette get painted rather than laid out as components.
    {
        auto row = sectionHeading ("Icons", 132);

        if (apply)
            iconBounds = row;
    }

    auto paletteRow = sectionHeading ("Palette", 150);

    if (apply)
    {
        paletteBounds = paletteRow;
        contentBottom = paletteRow.getBottom();
    }

    return paletteRow.getBottom();
}

void DewGallery::paint (juce::Graphics& g)
{
    g.fillAll (colour::background);

    g.setColour (colour::textPrimary);
    g.setFont (type::font (type::display, true));
    g.drawText ("dew design system",
                getLocalBounds().reduced (space::xxl).removeFromTop (34),
                juce::Justification::centredLeft, false);

    for (const auto& section : sections)
    {
        g.setColour (colour::textSecondary);
        g.setFont (type::font (type::small, true));
        g.drawText (section.title.toUpperCase(), section.bounds,
                    juce::Justification::centredLeft, false);

        g.setColour (colour::divider);
        g.drawHorizontalLine (section.bounds.getBottom(),
                              (float) section.bounds.getX(),
                              (float) getWidth() - (float) space::xxl);
    }

    // --- icons ---------------------------------------------------------------
    {
        const auto all = icons::all();
        auto area = iconBounds;

        constexpr int cell = 62;
        constexpr int iconSize = 26;
        const auto perRow = juce::jmax (1, area.getWidth() / cell);

        for (int i = 0; i < (int) all.size(); ++i)
        {
            const auto column = i % perRow;
            const auto row = i / perRow;

            const juce::Rectangle<int> cellBounds (area.getX() + column * cell,
                                                   area.getY() + row * 62, cell - 4, 58);

            auto glyph = cellBounds.withSizeKeepingCentre (iconSize, iconSize)
                             .withY (cellBounds.getY() + 6);

            icons::draw (g, all[(size_t) i].make(), glyph.toFloat(), colour::textPrimary);

            g.setColour (colour::textDisabled);
            g.setFont (type::font (8.0f));
            g.drawText (all[(size_t) i].name, cellBounds.withTop (glyph.getBottom() + 2),
                        juce::Justification::centredTop, false);
        }
    }

    // --- palette -------------------------------------------------------------
    {
        const auto swatches = palette();
        auto area = paletteBounds;

        constexpr int cell = 104;
        const auto perRow = juce::jmax (1, area.getWidth() / cell);

        for (int i = 0; i < (int) swatches.size(); ++i)
        {
            const auto column = i % perRow;
            const auto row = i / perRow;

            juce::Rectangle<int> cellBounds (area.getX() + column * cell,
                                             area.getY() + row * 48, cell - 6, 44);

            auto chip = cellBounds.removeFromTop (26);
            g.setColour (swatches[(size_t) i].value);
            g.fillRoundedRectangle (chip.toFloat(), radius::sm);
            g.setColour (colour::outline);
            g.drawRoundedRectangle (chip.toFloat(), radius::sm, stroke::hairline);

            g.setColour (colour::textSecondary);
            g.setFont (type::font (8.5f));
            g.drawText (swatches[(size_t) i].name, cellBounds, juce::Justification::centredTop, false);
        }
    }

    // --- inert area sample ---------------------------------------------------
    {
        auto sample = juce::Rectangle<int> (getWidth() - space::xxl - 200, space::xxl - 2, 200, 34);
        paint::inertArea (g, sample);
        g.setColour (colour::textDisabled);
        g.setFont (type::font (type::caption));
        g.drawText ("inert area", sample, juce::Justification::centred, false);
    }
}

} // namespace dew
