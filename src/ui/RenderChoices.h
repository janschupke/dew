#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "io/OfflineRenderer.h"

namespace dew::renderChoices
{

/** What a render can be written as, and at what rate and depth.

    One table, because there are two windows asking the same question now: the
    render dialog, which asks it about the render you are starting, and the
    Rendering page of Preferences, which asks it about every render after this
    one. Two copies would be two lists of sample rates that agree until one of
    them gains 192kHz.

    The FORMAT list is here and the availability note is not: the render dialog
    explains a missing lame encoder under its controls and Preferences greys the
    entry, which is a difference in what each window is for rather than a
    difference about what the formats are.
*/

/** A sample rate in Hz IS its item id, offset past ComboBox's reserved 0. */
inline constexpr int rateIdBase = 1000;

const juce::Array<RenderFormat>& formats();

/** Every rate, and the narrower list mp3 can take. */
const juce::Array<int>& allRates();
const juce::Array<int>& ratesFor (RenderFormat);

/** Whether a stored value is one of the offered ones - what
    Settings-validated-on-read leaves to the reader, because dew_app knows
    nothing about what a renderer supports. */
bool isOfferedRate (int hz);
bool isOfferedDepth (int bits);

/** Fills `box` with the rates `format` takes, keeping the current selection
    when that format still takes it. Ids are rateIdBase + the rate. */
void fillRates (juce::ComboBox& box, RenderFormat format);

/** Fills `box` with 16, 24 and float 32. The bit count IS the item id. */
void fillDepths (juce::ComboBox& box);

} // namespace dew::renderChoices
