#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <initializer_list>
#include <utility>
#include <vector>

#include "model/AutomationTargets.h"
#include "model/Ids.h"

namespace dew::demo
{

/** Node builders shared by the demo library.

    Every one of them starts from `defaultTreeFor (spec)` rather than assembling
    a node by hand. That is not a convenience: a hand-built node stops
    round-tripping the moment the schema gains a property, and the demos are
    byte-compared against the files committed under examples/, so a node of the
    wrong shape is a failing build rather than a quiet difference.

    Here rather than in ProjectFactory.cpp because the demos outgrew it. They
    are content, and they change for musical reasons; `createDefault` is the
    document File > New produces and changes for structural ones.

    Split across three translation units along the axis the vocabulary already
    has - the nodes a demo makes, the sound it dials in, and the layout it
    arranges them into - because the source gate caps a file at 400 code lines
    and a library of ten tracks needs more files, not bigger ones.
*/

/** One automation point.

    A struct rather than a pair so a demo can state a segment's SHAPE and BEND,
    which the pair form could not reach - and which nothing shipped had ever
    used, so every curve in the library was a straight line.
*/
struct Point
{
    double step = 0.0;
    double value = 0.5;
    double curve = 0.0;          ///< -1..1; positive holds high longer
    const char* shape = "curve"; ///< "curve" or "step"
};

/** One note, written the way a part is read: where, how long, how high, how hard.

    A struct rather than four positional arguments to `makeNote` because a demo
    writes them in runs of dozens, and a braced list of `{ 0, 4, 64, 0.8 }` rows
    is the only form in which a melody is legible in C++ at all.
*/
struct Hit
{
    int step = 0;
    int lengthSteps = 1;
    int pitch = 60;
    double velocity = 0.8;
};

using Params = std::initializer_list<std::pair<const juce::Identifier&, double>>;

/** A double as the FILE will hold it.

    ProjectSerializer writes six decimal places, so a value carrying more of
    them reads back as a DIFFERENT double: the JSON compares equal and the tree
    does not. That is the exact inverse of the drift the byte comparison exists
    to catch, and only the tree comparison can see it - a gain of 0.74
    normalised into a 0..1.5 fader is 0.4933333333333333, the file says
    0.493333, and the demo stops matching itself.

    Every double a demo builder writes goes through this. It is not rounding for
    tidiness: it is making the value the factory holds and the value the file
    holds the same number.
*/
double stored (double value);

// -----------------------------------------------------------------------------
// Nodes - DemoBuilders.cpp
// -----------------------------------------------------------------------------

juce::ValueTree makeChannel (int id, const juce::String& name, const juce::String& colour,
                             int basePitch, const juce::String& wave, int octave, double attack,
                             double decay, double sustain, double release, double volume = 0.8);

juce::ValueTree makeMixerTrack (int id, const juce::String& name);
juce::ValueTree makePlaylistTrack (const juce::String& name);
juce::ValueTree makePattern (int id, const juce::String& name, int lengthSteps);
juce::ValueTree makeNote (int channelId, int step, int lengthSteps, int pitch, double velocity);

/** The pattern with this id, renamed and resized - appended if it is not there.

    Find-or-create rather than append, because createDefault() already ships a
    "Pattern 1" and a demo that appended its own first pattern gave the project
    TWO patterns with id 1. Nothing rejects that; findPattern simply returns the
    first, so every clip pointing at pattern 1 resolved to the empty one.
*/
juce::ValueTree patternIn (juce::ValueTree project, int id, const juce::String& name,
                           int lengthSteps);

/** How many steps a bar of this project is worth.

    Every clip helper below goes through it. It used to be the constant 16,
    excused by "no demo sets a metre" - which was true, and stopped being true
    the moment the library grew a waltz and a piece in 7/8. The excuse also
    claimed a test asserted it; none ever did.
*/
int stepsPerBar (const juce::ValueTree& project);

/** A clip covering whole bars. `project` is asked for the metre, nothing else. */
juce::ValueTree makeClip (const juce::ValueTree& project, int patternId, int startBar,
                          int lengthBars);
juce::ValueTree makeAutomationClip (const juce::ValueTree& project, int automationId, int startBar,
                                    int lengthBars);

/** A clip placed on the step grid, for the one thing bars cannot say: a hit that
    lands off the bar line. That is what v20's finer clip grid bought, and no
    shipped project had ever used it.
*/
juce::ValueTree makeClipAtStep (int patternId, int startStep, int lengthSteps);

juce::ValueTree makeEffect (int id, const juce::String& type, Params params);

/** The two effects whose mode is a NAME rather than a number.

    `Params` carries doubles, because every other effect parameter is one. A
    filter mode written through it would put a 0.0 where the schema declares a
    string: the file would still read back, coerced, and the tree the factory
    built would no longer be the tree the file parsed to - which is a byte
    comparison away from a failing build and no distance at all from a silent
    lowpass.
*/
juce::ValueTree makeFilter (int id, const juce::String& mode, double cutoff, double resonance,
                            double mix = 1.0);
juce::ValueTree makeDistortion (int id, const juce::String& mode, double drive, double tone,
                                double outputGain, double mix = 1.0);

/** A curve.

    `points` is a vector rather than an initializer_list so a demo can BUILD one
    - a pump is the same two points once a beat for two bars, and writing
    sixteen rows by hand is how the three of them in the shipped library came to
    disagree about their own shape. A braced list still converts.
*/
juce::ValueTree makeAutomation (int id, AutomationScope scope, int targetId, int slot,
                                const juce::Identifier& param, std::vector<Point> points);

/** A real value written the way a POINT stores it: normalised into the target's
    own range, through the target's own curve.

    A demo that wants a filter to open to 6 kHz or a tempo to settle at 88 bpm
    otherwise has to write 0.826 and 0.379, and the shipped library did exactly
    that - a comment beside 0.4706 saying it means 126 bpm is a fact with two
    homes, and the one in the file is the one that does not move when a range
    does. This asks the same ParamSpec table the engine reads.
*/
double curveValue (AutomationScope scope, const juce::Identifier& param, double value);

/** The same for an effect slot, whose table depends on the effect's type. */
double curveValueIn (const juce::String& effectType, const juce::Identifier& param, double value);

/** Writes a run of notes onto one channel of a pattern. */
void notes (juce::ValueTree pattern, int channelId, std::initializer_list<Hit> hits);

/** Writes one stack of pitches, all starting together. */
void chord (juce::ValueTree pattern, int channelId, int step, int lengthSteps,
            std::initializer_list<int> pitches, double velocity);

/** Writes a drum part from a step string: `x` a hit, `X` an accent, `o` a ghost,
    anything else a rest.

    `stride` is how many steps one character is worth, so a bar of sixteenths
    reads as sixteen characters whether the project runs at four steps to a beat
    or at twelve. Without that, the swung and 32nd-grid demos would each be a
    wall of dots with a hit every third or every other one, and nobody could see
    the rhythm in the source.

    `offset` moves the whole line, which is how a shuffle is written: dew has no
    swing knob, so a swung offbeat is a line placed later than the straight one
    - two calls with the same string and different offsets, rather than a wall
    of hand-placed notes.
*/
void steps (juce::ValueTree pattern, int channelId, int pitch, juce::StringRef grid,
            double velocity, int lengthSteps = 1, int stride = 1, int offset = 0);

// -----------------------------------------------------------------------------
// Sound - DemoBuilderSound.cpp
// -----------------------------------------------------------------------------

/** Points one oscillator slot at a classic waveform and switches it on. */
void setClassicOsc (juce::ValueTree channel, int slot, const juce::String& wave, int octave,
                    double gain, int detuneCents = 0);

/** Points one oscillator slot at a wavetable and switches it on.

    `source` is "envelope" or "lfo": the first sweeps the morph across the
    length of a note, the second runs free at `rate`. A pad wants the second and
    a struck sound wants the first, which is why both are in the demo.
*/
void setWavetableOsc (juce::ValueTree channel, int slot, const juce::String& table, double position,
                      double mod, const juce::String& source, double rate, int unisonVoices,
                      double unisonDetune, int octave = 0, double gain = 0.8);

/** One row of the FM matrix: how much this slot modulates each of the three, and
    how much of it is heard directly.

    `out` 0 makes the slot a pure modulator, which is what the operator above a
    carrier is; `to` its own index is feedback, which is how a Reese gets its
    edge. The defaults everywhere else are the identity - every amount 0, every
    output 1 - so three slots sum in parallel, which is what the synth was
    before there was a matrix.
*/
void setFm (juce::ValueTree channel, int slot, double to1, double to2, double to3, double out);

/** A free-running LFO on one slot, in Hz. */
void setLfo (juce::ValueTree channel, int slot, const juce::String& wave, double rateHz,
             double toPitch, double toVolume, double toPan);

/** An LFO locked to the tempo. `division` is a note value - "quarter", "eighth"
    - and is a fraction of a WHOLE note, not of a beat.
*/
void setSyncedLfo (juce::ValueTree channel, int slot, const juce::String& wave,
                   const juce::String& division, double toPitch, double toVolume, double toPan);

/** Silences an oscillator slot without removing it - a slot is always there. */
void disableOsc (juce::ValueTree channel, int slot);

void setAmp (juce::ValueTree channel, double attack, double decay, double sustain, double release);

/** A channel's own level and placement, which no shipped demo had ever moved. */
void setMix (juce::ValueTree channel, double volume, double pan);

/** Routes a channel to a mixer insert by id. */
void routeTo (juce::ValueTree channel, int mixerTrackId);

// -----------------------------------------------------------------------------
// Layout - DemoBuilderLayout.cpp
// -----------------------------------------------------------------------------

/** A project with `numChannels` channels, inserts and playlist lanes.

    A channel's mixerTrackId is its own id, so a channel past the fourth needs a
    matching insert to exist or it routes nowhere anybody can see. Growing the
    three together is the only arrangement that stays true.
*/
juce::ValueTree scaffold (int numChannels);

void setSong (juce::ValueTree project, const juce::String& name, double tempoBpm, int bars);

/** The grid and the metre, written straight onto a project that has no music yet.

    Deliberately NOT ProjectEdits::setMeter, which rescales every clip to hold
    its position in steps. There is nothing here to hold: this runs before a
    demo has a clip, and a demo states its metre rather than changing it.
*/
void setGrid (juce::ValueTree project, int stepsPerBeat, int beatsPerBar, int beatUnit);

/** The channel with this id, or an invalid tree. */
juce::ValueTree channelWithId (const juce::ValueTree& project, int id);

/** The channel with this name, matched the way the score bake matches: case
    insensitively, because a score writes `pad` and a rack shows `Pad`.
*/
juce::ValueTree channelNamed (const juce::ValueTree& project, const juce::String& name);

juce::ValueTree mixerTrackWithId (const juce::ValueTree& project, int id);

/** The master bus, which is a bus like any other and had never been reached. */
juce::ValueTree masterOf (const juce::ValueTree& project);

void setMixerTrack (juce::ValueTree project, int id, double gain, double pan);

juce::ValueTree laneAt (const juce::ValueTree& project, int index);

/** A lane's own mute and gain: a spare idea kept beside the arrangement and
    silenced, which is the thing a lane mute is for.
*/
void setLane (juce::ValueTree project, int index, bool muted, double gain);

/** Drops the channels a score never adopted, and the empty pattern beside them.

    ScoreBake starts from createDefault(), so a compiled project carries four
    silent channels named Kick, Snare, Bass and Lead and an empty "Pattern 1"
    unless the score happened to use those names. Shipping them would put four
    dead rows at the top of the rack of a demo about something else.
*/
void pruneUnplayedChannels (juce::ValueTree project);

/** Drops the playlist lanes nothing was put on.

    The lane half of the same problem. A bake starts from createDefault(), which
    ships four empty lanes, and appends its own "Score" one AFTER them - so the
    lane holding the entire arrangement is the fifth, and a demo that then said
    setLanes({"Score", "Tempo"}) kept the first two empty ones and threw the
    arrangement away. Nothing complained: the clips were gone, the patterns were
    still there, and the render was still audible because an automation lane on
    a channel with a curve is not silence.
*/
void pruneEmptyLanes (juce::ValueTree project);

/** Gives every channel an insert of its own, named after it.

    A compiled score routes everything to insert 1: ProjectEdits::addChannel
    looks for a track whose id matches the channel's and falls back to the
    first, and a score that names five channels on a four-track project finds
    no match for any of them. A demo about the mixer arriving with five
    channels stacked on one fader is not a demo about the mixer.

    Ids stay contiguous from 1, because an automation clip addresses a track by
    id and a sparse mixer is a thing nothing else here produces.
*/
void rebuildInserts (juce::ValueTree project);

/** Makes the mixer exactly these inserts, ids 1..N, keeping their chains.

    What rebuildInserts is built on, and what a demo reaches for directly when
    the routing is not one insert per channel - a drum bus is four channels on
    one fader, and that is a thing the mixer can do that nothing shipped showed.
*/
void setInserts (juce::ValueTree project, const juce::StringArray& names);

/** Makes the playlist exactly these lanes, keeping the clips already on them. */
void setLanes (juce::ValueTree project, const juce::StringArray& names);

} // namespace dew::demo
