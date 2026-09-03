#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <vector>

namespace dew
{

/** Builds project trees from scratch. */
struct ProjectFactory
{
    /** A blank project: four channels routed to four mixer inserts, one empty
        16-step pattern, four empty playlist tracks. What File > New produces.
    */
    static juce::ValueTree createDefault();

    /** A four-bar demo used by the tests, by CI, and shipped as examples/demo.dew.
        Deliberately audible: a kick pulse, an offbeat bass, and a lead line, so a
        render that produces silence is obviously wrong.
    */
    static juce::ValueTree createDemo();

    /** The demo library: one project per part of the app, so each feature has
        something that already uses it rather than only a description.

        Shipped as files under examples/ and embedded in the binary, so the
        Demos menu works from a build with no source tree beside it.
    */
    struct Demo
    {
        const char* fileName;
        const char* menuName;
        const char* description;
        juce::ValueTree (*build)();
    };

    static const std::vector<Demo>& demos();

    /** Chords, held notes and varied velocities: what the piano roll is for. */
    static juce::ValueTree createMelodyDemo();

    /** A pad through a filter, a delay and a reverb, plus drive on the mix. */
    static juce::ValueTree createEffectsDemo();

    /** An arpeggio under a filter sweep and a fade, both as automation clips. */
    static juce::ValueTree createAutomationDemo();

    /** Five wavetables, unison stacks and a morph position drawn as a curve.

        Its notes come from examples/drift.score and its sound is built in
        Demos.cpp: the language owns the music, the user owns the instrument,
        and this demo is what that division looks like from both sides.
    */
    static juce::ValueTree createWavetableDemo();

    /** Three oscillators on every channel: octaves, cent detune and per-slot
        gain, with one wavetable slot standing among classic ones.
    */
    static juce::ValueTree createLayersDemo();

    /** Eight channels, six patterns and three lanes that come and go
        independently, with four drums sharing one mixer insert.
    */
    static juce::ValueTree createArrangementDemo();

    /** The score example, COMPILED rather than loaded.

        Opening it puts real text in the Score tab, so pressing Compile
        reproduces exactly what is already playing - and regenerating the demo
        library rebuilds the .dew from the .score, which is what stops the two
        drifting apart.
    */
    static juce::ValueTree createScoreDemo();
};

} // namespace dew
