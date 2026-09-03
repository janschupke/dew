#pragma once

#include <vector>

#include "lang/Diagnostics.h"
#include "lang/Model.h"

namespace dew::lang
{

/** One chord, and the steps it occupies within its section. */
struct ChordSpan
{
    int startStep = 0;
    int endStep = 0; ///< half-open
    Chord chord;

    /** The key a melody should draw scale tones from here - the home key,
        except under a tonicisation.
    */
    Key localKey;

    SourceRange origin;
};

/** Lays a progression out across a section.

    Absolute lengths are subtracted first; the weights then split what is left
    by LARGEST FRACTIONAL REMAINDER, ties to the earliest entry. That is
    deterministic, sums to exactly the space available, and cannot fail for any
    arrangement of weights - unlike rounding each share independently, which
    leaves or loses steps depending on the numbers.

    Three things are errors rather than quiet fixes:
      - a progression longer than its section;
      - one shorter, with no weights to absorb the difference, because trailing
        silence nobody asked for is the most expensive kind of bug in generated
        music: it sounds plausible;
      - a chord whose share rounds to nothing, which would vanish silently.

    A `|` written after an entry asserts that the running position is a bar
    line, and is checked once the shares are known.
*/
std::vector<ChordSpan> layOutHarmony (const HarmonySpec&, const Key& songKey, int totalSteps,
                                      int stepsPerBar, int beatUnit, int stepsPerBeat,
                                      DiagnosticBag&);

} // namespace dew::lang
