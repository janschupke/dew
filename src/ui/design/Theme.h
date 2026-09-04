#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace dew::theme
{

/** Which palette dew is painted from.

    Both are DARK. A light theme is a different job and not this one: three of
    the emphasis transforms and all four lift rungs encode "less important is
    darker" and "hovered is lighter", which is true on a dark ground and
    inverts on a light one. High contrast keeps every one of those correct and
    changes only how far apart the values sit.
*/
enum class Kind
{
    dark,        ///< as dew has always looked, held to WCAG AA
    highContrast ///< the same roles, held to AAA: 7:1 read, 4.5:1 found
};

/** Swaps the palette and makes every already-painted thing take it up.

    Two steps, because a palette has two kinds of reader. Everything that reads
    a colour inside paint() follows the swap on its next repaint and needs
    nothing. Everything that COPIED one - a LookAndFeel's ColourIds, a Label's
    textColourId, a toggle's on-colour - is holding a value from the palette
    that was in force when it was built, and has to be asked again.

    `root` is asked through Component::sendLookAndFeelChange, which recurses the
    whole tree calling lookAndFeelChanged(). That is JUCE's own hook for exactly
    this and the reason components override it rather than this function
    knowing what every panel captured.

    The look and feel is taken from Desktop rather than passed in. There is one
    installed instance and a caller handing over a different one would re-seed
    something nothing paints from - which is not a hypothetical: a ComboBox
    copies its text colour out of the LookAndFeel's own ColourIds, so the boxes
    are the first thing to come back wrong.

    Message thread only.
*/
void apply (Kind, juce::Component& root);

/** The palette in force, without a tree to refresh. Used at startup, before
    there is a window to tell. */
void applyPalette (Kind);

Kind current() noexcept;

/** The name a theme is STORED under, and the theme a stored name means.

    A name rather than an index, so adding a theme cannot silently renumber a
    choice somebody already made. An unknown name is the default rather than an
    error: a settings file written by a later version should open, not refuse.
*/
juce::String name (Kind);
Kind kindFor (const juce::String&);

} // namespace dew::theme
