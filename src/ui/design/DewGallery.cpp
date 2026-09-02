#include "ui/design/DewGallery.h"

#include <array>
#include <cmath>

#include "ui/design/Icons.h"
#include "ui/design/Tokens.h"

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
        // Both were missing, and the ramp's absence was why nothing outside a
        // test referred to it - a token the gallery does not show is a token
        // nobody knows they have.
        { "textOnAccent", colour::textOnAccent },
        { "channel 0", colour::channelColour (0) }, { "channel 1", colour::channelColour (1) },
        { "channel 2", colour::channelColour (2) }, { "channel 3", colour::channelColour (3) },
        { "channel 4", colour::channelColour (4) }, { "channel 5", colour::channelColour (5) },
        { "channel 6", colour::channelColour (6) }, { "channel 7", colour::channelColour (7) },
    };
}

struct Rung { const char* name; float value; };

/** The emphasis scale, which had no name until it had one.

    Alphas over the accent, then the surface lifts over a panel: the two halves
    are different questions - how faint is this, versus how much brighter does a
    surface get when touched - and showing them side by side is what stops the
    next person reaching for 0.07.
*/
std::vector<Rung> emphasisAlphas()
{
    return { { "tint", emphasis::tint }, { "wash", emphasis::wash },
             { "hatch", emphasis::hatch }, { "subdued", emphasis::subdued },
             { "dimmed", emphasis::dimmed }, { "strong", emphasis::strong } };
}

std::vector<Rung> emphasisLifts()
{
    return { { "surfaceLift", emphasis::surfaceLift }, { "controlLift", emphasis::controlLift },
             { "pressLift", emphasis::pressLift }, { "edgeLift", emphasis::edgeLift } };
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

    // The compact pair, as a channel rack row carries them: no caption and no
    // value, so the only thing telling volume from pan is which way each fills.
    auto* compactVolume = new DewKnob ("VOL", 0.0, 1.0, 0.001);
    compactVolume->setCompact (true);
    compactVolume->setTooltip ("Volume");
    compactVolume->setValue (0.8, juce::dontSendNotification);
    add (compactVolume);

    auto* compactPan = new DewKnob ("PAN", -1.0, 1.0, 0.001);
    compactPan->setCompact (true);
    compactPan->setBipolar (true);
    compactPan->setTooltip ("Pan");
    compactPan->setValue (-0.4, juce::dontSendNotification);
    add (compactPan);

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

    // --- signal scope, both states -------------------------------------------
    // Engine-less on purpose: neither starts a timer, so this page renders the
    // same way every time it is asked to.
    add (new SignalScope());   // never pushed - the empty state the bar shows
                               // when nothing is sounding

    auto* driven = new SignalScope();
    {
        // A tone, a harmonic well above it and a little noise, so both wells
        // have something to say: an unambiguous rising edge for the trace, and
        // energy at two separate places on the log axis for the bars.
        std::array<float, SignalScope::windowSamples> signal {};
        juce::Random random { 1234 };   // fixed seed: this page is a screenshot

        for (size_t i = 0; i < signal.size(); ++i)
        {
            const auto seconds = (double) i / 44100.0;
            const auto radians = 2.0 * juce::MathConstants<double>::pi * seconds;

            signal[i] = (float) (0.45 * std::sin (radians * 220.0)
                                 + 0.16 * std::sin (radians * 3000.0))
                        + 0.02f * (random.nextFloat() * 2.0f - 1.0f);
        }

        // One push is enough: the ballistics rise instantly, so the bars are at
        // full height here rather than caught part-way up.
        driven->pushFrame (signal.data(), (int) signal.size(), 44100.0);
    }

    add (driven);
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

        // The compact pair sits on the same baseline as the captioned knobs'
        // rotaries, which is where it would be if it were on a row.
        auto compact = row.withSizeKeepingCentre (row.getWidth(), size::knobSm);

        for (int i = 0; i < 2; ++i)
        {
            place (controls[index++], compact.removeFromLeft (size::knobSm));
            compact.removeFromLeft (space::lg);
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

    // Signal scope.
    {
        auto row = sectionHeading ("Signal scope - empty, and with a signal",
                                   size::controlHeight);

        for (int i = 0; i < 2; ++i)
        {
            place (controls[index++], row.removeFromLeft (SignalScope::preferredWidth)
                                          .withHeight (size::controlHeight));
            row.removeFromLeft (space::xl);
        }
    }

    // Every control the constructor added has to have been given bounds above.
    // The array is walked positionally with a count per section, so a control
    // added without a matching entry here used to shift every control after it
    // into someone else's slot - silently, on a page whose only reader is a PNG.
    jassert (index == controls.size());

    // Icons and palette get painted rather than laid out as components.
    {
        auto row = sectionHeading ("Icons", 132);

        if (apply)
            iconBounds = row;
    }

    auto emphasisRow = sectionHeading ("Emphasis", 96);

    if (apply)
        emphasisBounds = emphasisRow;

    auto paletteRow = sectionHeading ("Palette", 200);

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
            g.setFont (type::font (type::caption));
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
            g.setFont (type::font (type::caption));
            g.drawText (swatches[(size_t) i].name, cellBounds, juce::Justification::centredTop, false);
        }
    }

    // --- emphasis ------------------------------------------------------------
    {
        auto area = emphasisBounds;

        constexpr int cell = 104;
        const auto drawRow = [&] (juce::Rectangle<int> row, const std::vector<Rung>& rungs,
                                  bool overAccent)
        {
            for (int i = 0; i < (int) rungs.size(); ++i)
            {
                juce::Rectangle<int> cellBounds (row.getX() + i * cell, row.getY(), cell - 6, 40);

                auto chip = cellBounds.removeFromTop (24);

                g.setColour (colour::surface);
                g.fillRoundedRectangle (chip.toFloat(), radius::sm);

                g.setColour (overAccent ? colour::accent.withAlpha (rungs[(size_t) i].value)
                                        : colour::surface.brighter (rungs[(size_t) i].value));
                g.fillRoundedRectangle (chip.toFloat(), radius::sm);

                g.setColour (colour::textSecondary);
                g.setFont (type::font (type::caption));
                g.drawText (rungs[(size_t) i].name, cellBounds,
                            juce::Justification::centredTop, false);
            }
        };

        drawRow (area.removeFromTop (44), emphasisAlphas(), true);
        drawRow (area.removeFromTop (44), emphasisLifts(), false);
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
