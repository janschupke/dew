#pragma once

#include <string>
#include <vector>

#include "lang/Harmony.h"
#include "lang/Melody.h"
#include "lang/Model.h"
#include "lang/Rng.h"

namespace dew::lang
{

/** What one counterpoint rule did, so a warning can name it.

    Relaxation is reported, never silent. A voice that went somewhere it was
    told not to is a thing the writer has to know about; a voice that fell
    silent because the rules left nothing to sing is worse.
*/
struct Relaxation
{
    CounterpointRule rule = CounterpointRule::parallelFifths;
    int bar = 0;
};

struct CounterpointResult
{
    std::vector<MelodyNote> notes;
    std::vector<Relaxation> relaxations;
};

/** Generates one voice against voices already written.

    A beam search of width 8 over the onsets, not a constraint solver. Three
    reasons, in order: a solver's failure modes are "unsatisfiable" and "twenty
    seconds", both fatal in an editor that recompiles as you type; the cases a
    beam loses are close to inaudible next to the machinery; and a beam's choice
    can be explained in a diagnostic, which a solver's cannot.

    Cost is additive along the timeline, so the beam is an exact dynamic program
    over the states it keeps. Hard rules filter the candidates at each onset and
    soft rules score the transition into it.

    `against` gives, for each onset, the pitch the other voice is sounding, or
    -1 where it is silent. Several voices are handled by generating in declared
    order and scoring each against every voice before it - `O(n)` passes, no
    joint search, and the same reason: what a joint search would buy is not
    audible next to what it costs.

    `ownIsAbove` says which side of the other voice this one sits on, and it is
    the CALLER's to decide - from the two channels' declared ranges, which is
    the only place the answer is actually written down. Inferring it here from
    the first note the other voice happens to play gets it wrong whenever the
    two lines start close together, and then `voice-crossing forbid` forbids the
    wrong direction and every note crosses.
*/
CounterpointResult generateCounterpoint (const std::vector<Onset>& onsets,
                                         const std::vector<ChordSpan>& spans,
                                         const std::vector<std::vector<int>>& against,
                                         const CounterpointSpec&,
                                         int lowPitch, int highPitch,
                                         bool ownIsAbove,
                                         int stepsPerBar,
                                         const SeedPath&);

/** The order hard rules are given up in when they leave nothing to sing.

    Declared, and the same every time: relaxing in an order that depended on the
    rules written would make one score's failure mode depend on another's.
*/
const std::vector<CounterpointRule>& relaxationOrder();

const char* nameOf (CounterpointRule) noexcept;

} // namespace dew::lang
