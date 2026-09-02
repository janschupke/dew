#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

/** The icon set, drawn as juce::Path rather than loaded as assets.

    Each function returns a path in a 0..1 square, so a caller scales it into
    whatever bounds it has and fills it with whatever colour the theme says.
    That is the whole reason for doing it this way: an icon has no baked-in size
    or colour, the repository stays text-only and diffable, and adding one is a
    function rather than a build step.
*/
namespace dew::icons
{

// --- transport ---------------------------------------------------------------
juce::Path play();
juce::Path pause();
juce::Path stop();
juce::Path rewind();
juce::Path record();
juce::Path loop();

// --- editing -----------------------------------------------------------------
juce::Path plus();
juce::Path minus();
juce::Path trash();
juce::Path duplicate();
juce::Path pencil();
juce::Path magnet();          ///< snap to grid
juce::Path pointer();
juce::Path eraser();
juce::Path scissors();        ///< the slice tool
juce::Path dice();            ///< randomize
juce::Path quantize();        ///< align notes to the grid

// --- state -------------------------------------------------------------------
juce::Path mute();            ///< speaker with a cross
juce::Path solo();            ///< headphones
juce::Path power();           ///< effect bypass
juce::Path check();          ///< a ticked menu item
juce::Path lock();
juce::Path chevronUp();
juce::Path chevronDown();
juce::Path chevronRight();
juce::Path grip();            ///< drag handle

// --- waveforms ---------------------------------------------------------------
juce::Path waveSine();
juce::Path waveSaw();
juce::Path waveSquare();
juce::Path waveTriangle();

// --- effects -----------------------------------------------------------------
juce::Path effectFilter();
juce::Path effectReverb();
juce::Path effectDelay();
juce::Path effectDrive();
juce::Path effectChorus();
juce::Path effectEq();

// --- views -------------------------------------------------------------------
juce::Path zoomIn();
juce::Path zoomOut();
juce::Path fitToContent();
juce::Path automation();      ///< a curve with points

/** Scales `path` to fit `bounds` while keeping its proportions and centring it. */
juce::Path fitted (juce::Path path, juce::Rectangle<float> bounds);

/** Draws an icon centred in `bounds` in the given colour. */
void draw (juce::Graphics&, const juce::Path&, juce::Rectangle<float> bounds, juce::Colour);

/** Every icon with its name, for the gallery and for tests that must fail when
    an icon is added but never drawn anywhere.
*/
struct NamedIcon { const char* name; juce::Path (*make)(); };
std::vector<NamedIcon> all();

} // namespace dew::icons
