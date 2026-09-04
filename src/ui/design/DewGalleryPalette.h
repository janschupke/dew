#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** The gallery's colour page: every token dew declares, as a grid of chips.

    Its own translation unit rather than a third of DewGallery.cpp, because it
    is the part that GROWS. The rest of that file is the primitives page - a
    fixed list of controls laid out positionally - while this is a table of
    every colour the design system has, and it gets a row longer every time one
    is added. Keeping them together is what took DewGallery.cpp past the length
    gate the moment the function palette landed.

    Internal to dew_design. Nothing outside the gallery draws swatches.
*/
namespace dew::galleryPalette
{

/** How tall the grid comes out at this width.

    Asked rather than assumed: the palette's height used to be a hard-coded 200,
    which is the same defect the icon grid had - a section sized by a number
    instead of by a count draws over the one below it as soon as it grows.
*/
int heightFor (int width);

void paintSwatches (juce::Graphics&, juce::Rectangle<int> bounds);

} // namespace dew::galleryPalette
