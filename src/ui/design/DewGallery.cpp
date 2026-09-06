#include "ui/primitives/DewPaint.h"
#include "ui/design/DewGallery.h"

#include <array>
#include <cmath>

#include "i18n/Strings.h"
#include "model/Ids.h"
#include "model/ModuleCatalog.h"
#include "ui/design/DewGalleryPalette.h"
#include "ui/design/Icons.h"
#include "ui/primitives/DewSearchField.h"
#include "ui/design/ParamPalette.h"
#include "ui/design/Tokens.h"

namespace dew
{

using namespace tokens;

namespace
{

/** The page title's strip. Stated once: layOut() skips it and paint() draws
    into it, and they used to say 44 and 34 independently. */
constexpr int titleHeight = size::rowHeight;

/** One icon's cell on the icon grid, and the glyph inside it.

    Stated once for the same reason titleHeight is: layOut() has to know how
    many rows the icons need and paint() has to draw them into the same grid.
    The height used to be a hard-coded 132 in one and a 62 in the other, so
    adding an icon that wrapped to a third row drew it over the next section.
*/
constexpr int iconCell = 62;
constexpr int iconGlyph = 26;

struct Rung
{
    const char* name;
    float value;
};

/** The emphasis scale, which had no name until it had one.

    Alphas over the accent, then the surface lifts over a panel: the two halves
    are different questions - how faint is this, versus how much brighter does a
    surface get when touched - and showing them side by side is what stops the
    next person reaching for 0.07.
*/
std::vector<Rung> emphasisAlphas()
{
    return { { "tint", emphasis::tint },     { "wash", emphasis::wash },
             { "hatch", emphasis::hatch },   { "subdued", emphasis::subdued },
             { "dimmed", emphasis::dimmed }, { "strong", emphasis::strong } };
}

std::vector<Rung> emphasisLifts()
{
    return { { "surfaceLift", emphasis::surfaceLift },
             { "controlLift", emphasis::controlLift },
             { "pressLift", emphasis::pressLift },
             { "edgeLift", emphasis::edgeLift } };
}

} // namespace

DewGallery::DewGallery()
{
    setComponentID ("gallery");

    const auto add = [this] (juce::Component* c)
    {
        controls.add (c);
        addAndMakeVisible (c);
        return c;
    };

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
    // One of each role, because a role is a colour and a page that shows only
    // the neutral one is a page that does not show the roles at all.
    add (new DewIconButton (icons::play(), "Play", DewIconButton::Role::go));
    add (new DewIconButton (icons::record(), "Record", DewIconButton::Role::record));
    add (new DewIconButton (icons::stop(), "Stop"));

    auto* looping = new DewIconButton (icons::loop(), "Loop");
    looping->setClickingTogglesState (true);
    looping->setToggleState (true, juce::dontSendNotification);
    add (looping);

    add (new DewIconButton (icons::plus(), "Add"));
    add (new DewIconButton (icons::trash(), "Delete", DewIconButton::Role::danger));

    // The on/off indicator every row carries, on the reference page as it is
    // everywhere else. It was an "M" and an "S" here - the last two letters in
    // the application, on the one surface whose job is to say what the set is.
    auto* off = new DewIconButton (icons::power(), "Turn off");
    off->setClickingTogglesState (true);
    off->setOnColour (colour::warning);
    off->setToggleState (true, juce::dontSendNotification);
    add (off);

    add (new DewLetterToggle ("R", colour::recording, "Arm for recording"));

    // A disabled one, because that is a state this page had no sample of - and
    // the state that was wrong: a disabled icon button used to dim only its
    // glyph and keep its fill and border at full strength, so it read as an
    // ordinary button with a faint icon.
    auto* noPresets = new DewIconButton (icons::preset(), "No presets for this effect");
    noPresets->setEnabled (false);
    add (noPresets);

    // --- knobs ---------------------------------------------------------------
    // A table rather than nine near-identical blocks, because the page is a
    // list of specimens and reads better as one. The ORDER is load-bearing:
    // layOut walks `controls` positionally, and the captioned ones have to come
    // before the compact pair.
    //
    // Each takes its function colour through the same roleOf the application
    // does, rather than being handed one. A gallery knob coloured by hand would
    // be a page showing what the design system is SAID to do.
    struct Specimen
    {
        const char* caption;
        const juce::Identifier* property;
        double minimum, maximum, interval, value;
        int decimals;
        bool bipolar;
        const char* tooltip; ///< non-empty makes it compact, as a row carries it
    };

    // Seven captioned - two pairs of which share a shape and a range and mean
    // different things - then the compact pair a channel rack row holds, where
    // there is no caption and the colour is all there is.
    const Specimen specimens[] {
        { "CUTOFF", &ids::cutoff, 20.0, 20000.0, 1.0, 2400.0, 0, false, "" },
        { "RES", &ids::resonance, 0.0, 1.0, 0.001, 0.35, 3, false, "" },
        { "PAN", &ids::pan, -1.0, 1.0, 0.001, -0.4, 3, true, "" },
        { "ATTACK", &ids::attack, 0.0005, 10.0, 0.0005, 0.05, 3, false, "" },
        { "SIZE", &ids::roomSize, 0.0, 1.0, 0.01, 0.6, 2, false, "" },
        { "DEPTH", &ids::depth, 0.0, 1.0, 0.01, 0.3, 2, false, "" },
        { "OCT", &ids::octave, -4.0, 4.0, 1.0, -1.0, 0, true, "" },
        { "VOL", &ids::volume, 0.0, 1.0, 0.001, 0.8, 3, false, "Volume" },
        { "PAN", &ids::pan, -1.0, 1.0, 0.001, -0.4, 3, true, "Pan" },
    };

    for (const auto& specimen : specimens)
    {
        auto* knob = new DewKnob (specimen.caption, specimen.minimum, specimen.maximum,
                                  specimen.interval);

        knob->setNumDecimalPlaces (specimen.decimals);
        knob->setBipolar (specimen.bipolar);
        knob->setFunctionColour (palette::forRole (roleOf (*specimen.property)));

        if (juce::String (specimen.tooltip).isNotEmpty())
        {
            knob->setCompact (true);
            knob->setTooltip (specimen.tooltip);
        }

        knob->setValue (specimen.value, juce::dontSendNotification);
        add (knob);
    }

    // --- number fields -------------------------------------------------------
    auto* tempo = new DewNumberField();
    tempo->setRange (20.0, 300.0, 0.1);
    tempo->setValue (128.0, juce::dontSendNotification);
    tempo->setNumDecimalPlaces (1);
    tempo->setSuffix (tr (StringId::unit_bpm));
    add (tempo);

    auto* steps = new DewNumberField();
    steps->setRange (1.0, 256.0, 1.0);
    steps->setValue (32.0, juce::dontSendNotification);
    steps->setNumDecimalPlaces (0);
    steps->setCaption ("STEPS");
    add (steps);

    // The field twin of the OCT knob above, and the page's one specimen of the
    // field treatment: a field takes its function colour on the edge it shows
    // while being dragged, not on its resting border or its number.
    auto* octave = new DewNumberField();
    octave->setRange (-4.0, 4.0, 1.0);
    octave->setValue (-1.0, juce::dontSendNotification);
    octave->setNumDecimalPlaces (0);
    octave->setCaption ("OCT");
    octave->setFunctionColour (palette::forRole (roleOf (ids::octave)));
    add (octave);

    // --- dropdowns -----------------------------------------------------------
    // Stock JUCE widgets styled by DewLookAndFeel rather than dew primitives,
    // so this is where a change to that styling gets seen.
    auto* wave = new DewDropdown();
    wave->addItemList ({ "Sine", "Saw", "Square", "Triangle" }, 1);
    wave->setSelectedId (2, juce::dontSendNotification);
    add (wave);

    auto* routing = new DewDropdown();
    routing->addItemList ({ "Insert 1", "Insert 2", "Insert 3", "Insert 4" }, 1);
    routing->setSelectedId (1, juce::dontSendNotification);
    add (routing);

    auto* unavailable = new DewDropdown();
    unavailable->addItem (tr (StringId::gallery_unavailable), 1);
    unavailable->setSelectedId (1, juce::dontSendNotification);
    unavailable->setEnabled (false);
    add (unavailable);

    // --- the search field ----------------------------------------------------
    // Shown with something typed in it, because the clear button only exists
    // when there is something to clear - an empty one would be a picture of
    // half the control.
    auto* search = new DewSearchField();
    search->setPlaceholder (tr (StringId::gallery_searchPlaceholder));
    search->setTooltip (tr (StringId::gallery_searchPlaceholder));
    search->setClearTooltip (tr (StringId::gallery_searchClear));
    search->setText (tr (StringId::gallery_searchTyped), juce::dontSendNotification);
    add (search);

    // --- signal scope, both states -------------------------------------------
    // Engine-less on purpose: neither starts a timer, so this page renders the
    // same way every time it is asked to.
    add (new SignalScope()); // never pushed - the empty state the bar shows
                             // when nothing is sounding

    auto* driven = new SignalScope();
    {
        // A tone, a harmonic well above it and a little noise, so both wells
        // have something to say: an unambiguous rising edge for the trace, and
        // energy at two separate places on the log axis for the bars.
        std::array<float, SignalScope::windowSamples> signal {};
        juce::Random random { 1234 }; // fixed seed: this page is a screenshot

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

    // The page title is painted in this strip; sections start below it. Its
    // height is stated once, here, and paint() takes it from the same place.
    area.removeFromTop (titleHeight + space::md);

    int index = 0;

    const auto place = [&] (juce::Component* c, juce::Rectangle<int> bounds)
    {
        if (apply)
            c->setBounds (bounds);
    };

    /** The control's own answer, so the reference page shows what the rest of
        the application does. It wrote a bare 40 for a captioned number field
        beside dropdowns at 26 - encoding the very mismatch this page exists to
        make visible. */
    const auto sized = [] (juce::Component* c, juce::Rectangle<int> bounds)
    {
        if (auto* field = dynamic_cast<DewNumberField*> (c))
            return bounds.withHeight (field->preferredHeight());

        return bounds.withHeight (size::controlHeight);
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

        for (int i = 0; i < 9; ++i)
        {
            place (controls[index++],
                   row.removeFromLeft (size::iconButton + 6).withHeight (size::iconButton + 6));
            row.removeFromLeft (space::md);
        }
    }

    // Knobs.
    {
        auto row = sectionHeading ("Knobs", 78);

        // Seven, and the number is here rather than derived because the array
        // is walked positionally: a knob added without this moving takes the
        // compact pair's slot and every control after it shifts by one.
        for (int i = 0; i < 7; ++i)
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
        auto row = sectionHeading ("Number fields - drag up and down to change",
                                   size::controlHeight + size::captionBand);

        for (const auto width : { 120, 86, 86 })
        {
            place (controls[index], sized (controls[index], row.removeFromLeft (width)));
            ++index;
            row.removeFromLeft (space::md);
        }
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

    // Search field.
    {
        auto row = sectionHeading ("Search field", size::controlHeight);

        place (controls[index++], row.removeFromLeft (240).withHeight (size::controlHeight));
    }

    // Signal scope.
    {
        auto row = sectionHeading ("Signal scope - empty, and with a signal", size::controlHeight);

        for (int i = 0; i < 2; ++i)
        {
            place (
                controls[index++],
                row.removeFromLeft (SignalScope::preferredWidth).withHeight (size::controlHeight));
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
        // Derived from how many icons there ARE. It was a hard-coded 132, which
        // is two rows of them - so the three icons that took the grid to a third
        // row drew it straight over the section below, on the one page whose
        // whole job is to show what the design system looks like.
        const auto perRow = juce::jmax (1, area.getWidth() / iconCell);
        const auto rows = ((int) icons::all().size() + perRow - 1) / perRow;

        auto row = sectionHeading ("Icons", rows * iconCell);

        if (apply)
            iconBounds = row;
    }

    auto emphasisRow = sectionHeading ("Emphasis", 96);

    if (apply)
        emphasisBounds = emphasisRow;

    // Derived from how many swatches there ARE, the way the icon grid above is
    // and for the same reason: this was a hard-coded 200, and the palette is
    // the section a design system grows.
    auto paletteRow = sectionHeading ("Palette", galleryPalette::heightFor (area.getWidth()));

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
    g.drawText (tr (StringId::gallery_title),
                getLocalBounds().reduced (space::xxl).removeFromTop (titleHeight),
                juce::Justification::centredLeft, false);

    for (const auto& section : sections)
    {
        g.setColour (colour::textSecondary);
        g.setFont (type::font (type::small, true));
        g.drawText (section.title.toUpperCase(), section.bounds, juce::Justification::centredLeft,
                    false);

        g.setColour (colour::divider);
        g.drawHorizontalLine (section.bounds.getBottom(), (float) section.bounds.getX(),
                              (float) getWidth() - (float) space::xxl);
    }

    // --- icons ---------------------------------------------------------------
    {
        const auto all = icons::all();
        auto area = iconBounds;

        const auto perRow = juce::jmax (1, area.getWidth() / iconCell);

        for (int i = 0; i < (int) all.size(); ++i)
        {
            const auto column = i % perRow;
            const auto row = i / perRow;

            const juce::Rectangle<int> cellBounds (area.getX() + column * iconCell,
                                                   area.getY() + row * iconCell, iconCell - 4,
                                                   iconCell - 4);

            auto glyph = cellBounds.withSizeKeepingCentre (iconGlyph, iconGlyph)
                             .withY (cellBounds.getY() + 6);

            icons::draw (g, all[(size_t) i].make(), glyph.toFloat(), colour::textPrimary);

            g.setColour (colour::textDisabled);
            g.setFont (type::font (type::caption));
            g.drawText (all[(size_t) i].name, cellBounds.withTop (glyph.getBottom() + 2),
                        juce::Justification::centredTop, false);
        }
    }

    galleryPalette::paintSwatches (g, paletteBounds);

    // --- emphasis ------------------------------------------------------------
    {
        auto area = emphasisBounds;

        constexpr int cell = 104;
        const auto drawRow =
            [&] (juce::Rectangle<int> row, const std::vector<Rung>& rungs, bool overAccent)
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
                g.drawText (rungs[(size_t) i].name, cellBounds, juce::Justification::centredTop,
                            false);
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
        g.drawText (tr (StringId::gallery_inertArea), sample, juce::Justification::centred, false);
    }
}

} // namespace dew
