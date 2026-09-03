#pragma once

#include <vector>

#include "lang/Model.h"

namespace dew::lang
{

/** Lays a chord out as actual pitches, ascending.

    A candidate generator plus a cost function, and nothing more. Candidates are
    every inversion at every octave that fits the register, with the spread
    applied as a structural transform rather than searched for; the cost scores
    voice motion from the previous voicing, spacing, range and parallel perfect
    intervals, and the argmin wins with ties broken by candidate order.

    GREEDY, chord to chord. No backtracking and no constraint solver, for three
    reasons in order: a solver's failure modes are "unsatisfiable" and "twenty
    seconds", both fatal in an editor that recompiles on idle; the cases greedy
    loses - pre-positioning a voicing three chords early to dodge a later leap -
    are close to inaudible next to the machinery; and greedy's choice can be
    explained in a diagnostic, which a solver's cannot.

    If it ever proves audibly wrong the sanctioned upgrade is a beam search of
    width 4 over this same cost function. That stays deterministic and
    sub-millisecond. Nothing beyond it.

    `previous` may be empty, for the first chord.
*/
std::vector<int> voiceChord (const Chord&, const VoicingSpec&, const std::vector<int>& previous);

/** The cost of moving from one voicing to another, exposed so a test can assert
    that smooth leading actually reduces it rather than merely running.
*/
int voiceLeadingCost (const std::vector<int>& from, const std::vector<int>& to);

} // namespace dew::lang
