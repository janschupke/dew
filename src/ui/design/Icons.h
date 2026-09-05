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
juce::Path magnet(); ///< snap to grid
juce::Path pointer();
juce::Path eraser();
juce::Path scissors(); ///< the slice tool
juce::Path dice();     ///< randomize
juce::Path quantize(); ///< align notes to the grid

// --- state -------------------------------------------------------------------
juce::Path mute();  ///< speaker with a cross
juce::Path solo();  ///< headphones
juce::Path power(); ///< effect bypass
juce::Path check(); ///< a ticked menu item
juce::Path cross(); ///< clears a field: the tick's opposite, and the same weight
juce::Path lock();
juce::Path chevronUp();
juce::Path chevronDown();
juce::Path chevronLeft();
juce::Path chevronRight();
juce::Path grip(); ///< drag handle

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
juce::Path effectDistortion();
juce::Path effectChorus();
juce::Path effectPhaser();
juce::Path effectEq();
juce::Path effectCompressor();
juce::Path effectLimiter();

// --- views -------------------------------------------------------------------
juce::Path zoomIn();
juce::Path zoomOut();

/** The bare magnifier: looking WITHOUT changing what is shown.

    zoomIn and zoomOut are the same glass with a plus and a minus laid over it,
    so the three read as one family - which is the point, because a search box
    and a zoom button are both "find what you are after".
*/
juce::Path search();
juce::Path fitToContent();

/** The vertical axis of the same idea: how tall a row is, drawn as how many
    rows fit. Not chevrons - the piano roll's toolbar already spends a chevron
    pair on transposing a semitone, and two identical pairs meaning different
    things in one 34px strip is the drift the design system exists to stop.
*/
juce::Path rowsShorter(); ///< three rules: denser rows
juce::Path rowsTaller();  ///< two rules: roomier rows
juce::Path fitRows();     ///< fitToContent, turned through a right angle

juce::Path automation(); ///< a curve with points
juce::Path preset();     ///< a saved sound to load

/** Scales `path` to fit `bounds` while keeping its proportions and centring it. */
juce::Path fitted (juce::Path path, juce::Rectangle<float> bounds);

/** Draws an icon centred in `bounds` in the given colour. */
void draw (juce::Graphics&, const juce::Path&, juce::Rectangle<float> bounds, juce::Colour);

/** Every icon with its name, for the gallery and for the tests that check each
    one actually marks pixels inside its bounds and that no two share a name.

    Registering here is what puts an icon in the gallery, so an icon left out of
    this list is drawn by nothing and checked by nothing.
*/
struct NamedIcon
{
    const char* name;
    juce::Path (*make)();
};
std::vector<NamedIcon> all();

} // namespace dew::icons
