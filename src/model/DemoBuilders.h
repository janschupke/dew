#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <initializer_list>
#include <utility>

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

using Params = std::initializer_list<std::pair<const juce::Identifier&, double>>;

juce::ValueTree makeChannel (int id, const juce::String& name, const juce::String& colour,
                             int basePitch, const juce::String& wave, int octave,
                             double attack, double decay, double sustain, double release,
                             double volume = 0.8);

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
juce::ValueTree makeClip (int patternId, int startBar, int lengthBars);
juce::ValueTree makeAutomationClip (int automationId, int startBar, int lengthBars);

juce::ValueTree makeEffect (int id, const juce::String& type, Params params);

juce::ValueTree makeAutomation (int id, const juce::String& name, AutomationScope scope,
                                int targetId, int slot, const juce::Identifier& param,
                                std::initializer_list<Point> points);

/** A project with `numChannels` channels, inserts and playlist lanes.

    A channel's mixerTrackId is its own id, so a channel past the fourth needs a
    matching insert to exist or it routes nowhere anybody can see. Growing the
    three together is the only arrangement that stays true.
*/
juce::ValueTree scaffold (int numChannels);

/** The channel with this id, or an invalid tree. */
juce::ValueTree channelWithId (const juce::ValueTree& project, int id);

/** The channel with this name, matched the way the score bake matches: case
    insensitively, because a score writes `pad` and a rack shows `Pad`.
*/
juce::ValueTree channelNamed (const juce::ValueTree& project, const juce::String& name);

/** Points one oscillator slot at a classic waveform and switches it on. */
void setClassicOsc (juce::ValueTree channel, int slot, const juce::String& wave,
                    int octave, double gain, int detuneCents = 0);

/** Points one oscillator slot at a wavetable and switches it on.

    `source` is "envelope" or "lfo": the first sweeps the morph across the
    length of a note, the second runs free at `rate`. A pad wants the second and
    a struck sound wants the first, which is why both are in the demo.
*/
void setWavetableOsc (juce::ValueTree channel, int slot, const juce::String& table,
                      double position, double mod, const juce::String& source, double rate,
                      int unisonVoices, double unisonDetune,
                      int octave = 0, double gain = 0.8);

/** Silences an oscillator slot without removing it - a slot is always there. */
void disableOsc (juce::ValueTree channel, int slot);

void setAmp (juce::ValueTree channel, double attack, double decay, double sustain, double release);

/** Routes a channel to a mixer insert by id. */
void routeTo (juce::ValueTree channel, int mixerTrackId);

/** Drops the channels a score never adopted, and the empty pattern beside them.

    ScoreBake starts from createDefault(), so a compiled project carries four
    silent channels named Kick, Snare, Bass and Lead and an empty "Pattern 1"
    unless the score happened to use those names. Shipping them would put four
    dead rows at the top of the rack of a demo about something else.
*/
void pruneUnplayedChannels (juce::ValueTree project);

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
