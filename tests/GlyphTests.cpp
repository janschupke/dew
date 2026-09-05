// The join between a concept and its picture.
//
// dew_model says what kinds of thing exist; dew_design says what shapes there
// are; ui/design/Glyphs.h says which is which. Every mapping there is a switch
// with no default, so a missing case is a compile error and does not need a
// test. What needs one is the other half: that no two members of a vocabulary
// were given the SAME shape, which compiles perfectly and is invisible until
// two effects wear one icon in the picker that chooses between them.

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/EffectType.h"
#include "model/InstrumentType.h"
#include "model/ModuleCatalog.h"
#include "ui/design/Glyphs.h"

using namespace dew;

namespace
{

/** Every path drawn, and no two of them the same shape.

    Path::toString is the whole geometry, so two entries that came back with the
    same string are literally the same picture - which is the mistake a copied
    switch case makes.
*/
template <typename Range, typename GlyphFor>
void everyOneDistinct (const Range& vocabulary, GlyphFor glyphFor, const char* what)
{
    juce::StringArray shapes;

    for (const auto& member : vocabulary)
    {
        const auto path = glyphFor (member);

        INFO (what << " entry " << shapes.size());
        REQUIRE (! path.isEmpty());
        shapes.add (path.toString());
    }

    REQUIRE (shapes.size() > 0);

    const auto before = shapes.size();
    shapes.removeDuplicates (false);

    INFO (what << ": " << (before - shapes.size()) << " of " << before << " share a shape");
    CHECK (shapes.size() == before);
}

} // namespace

TEST_CASE ("every effect has its own glyph", "[design][glyphs]")
{
    // Over the CATALOG rather than over a written-out list of the ten, so an
    // effect added to the registry is one this test covers without being told.
    everyOneDistinct (
        effectDescriptors(), [] (const EffectDescriptor& e) { return glyph::forEffect (e.type); },
        "effect");
}

TEST_CASE ("every instrument has its own glyph", "[design][glyphs]")
{
    everyOneDistinct (
        instrumentDescriptors(),
        [] (const InstrumentDescriptor& i) { return glyph::forInstrument (i.type); }, "instrument");

    // The three are the whole vocabulary, and the catalog is what the menu is
    // built from - so a fourth kind that never reached the registry would show
    // up here rather than as a submenu quietly missing a row.
    CHECK ((int) instrumentDescriptors().size() == kNumInstrumentTypes);
}

TEST_CASE ("every waveform has its own glyph", "[design][glyphs]")
{
    const Waveform all[] { Waveform::sine, Waveform::saw, Waveform::square, Waveform::triangle };
    everyOneDistinct (all, [] (Waveform w) { return glyph::forWaveform (w); }, "waveform");
}

TEST_CASE ("every action has its own glyph", "[design][glyphs]")
{
    everyOneDistinct (
        glyph::allActions, [] (glyph::Action a) { return glyph::forAction (a); }, "action");

    // forAction is a switch with no default, so a NEW action cannot compile
    // without a shape. It can, however, compile without reaching allActions -
    // which is the list the gallery and the test above walk. The count is what
    // notices.
    CHECK (std::size (glyph::allActions) == 9);
}

TEST_CASE ("a waveform's glyph is the one its stored name means", "[design][glyphs]")
{
    // The oscillator's dropdown maps its rows through waveformFromString rather
    // than by counting them, so the reader the file uses and the glyph the
    // picker shows are the same fact. If they ever disagree, a project would
    // open showing one shape and sounding like another.
    for (const auto* name : { "sine", "saw", "square", "triangle" })
    {
        const auto wave = waveformFromString (name);

        INFO ("waveform: " << name);
        CHECK (waveformToString (wave) == juce::String (name));
        CHECK (! glyph::forWaveform (wave).isEmpty());
    }
}
