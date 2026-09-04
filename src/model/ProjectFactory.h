#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <vector>

#include "i18n/Strings.h"

namespace dew
{

/** Builds project trees from scratch. */
struct ProjectFactory
{
    /** A blank project: four channels routed to four mixer inserts, one empty
        16-step pattern, four empty playlist tracks. What File > New produces.

        Its names are written in `locale`, and they are written INTO THE FILE.
        A German reader's new project holds German channel names from the moment
        it exists; reopening it never re-translates them, and a project shared
        with someone reading English reads the same for both. That is what a
        name in a document is: the author's data, not a label.

        The DEFAULT is the reference locale, and the default being the safe
        answer is the whole point. Everything that compares what it built
        against a committed file - the demo builders, ScoreBake's fresh project,
        fifty test fixtures that name "Kick" - takes it and is right without
        saying anything. Only File > New wants the reader's language, and those
        three call sites say so.

        It used to be the other way round, and by accident: the names were
        literals, nothing in a console tool happens to call setLocale, and
        `dew_render --write-demos` therefore happened to write English. An
        accident that holds is one that stops holding the day a second
        catalogue ships and a test somewhere sets a locale before the artefact
        tests run. Now the tag is an argument, the risky value is the one you
        have to spell, and the safe one is what you get by not thinking.

        One consequence worth knowing: a score that says `channel kick` binds to
        a project channel BY NAME, so in another language it creates a channel
        rather than adopting one. That is the same behaviour as any other name
        mismatch - see .ai/rules/i18n.md, which writes it down.
    */
    static juce::ValueTree createDefault (juce::StringRef locale = referenceLocale());

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
        /** The stable id, and what the file is called on disk. */
        const char* fileName;

        /** What the Demos menu shows. A StringId outright, with nothing to keep
            in step: unlike a preset's name, this is never written into a file -
            the demo's own project name is - so there is no stored copy that
            could disagree with it. */
        StringId menuName;
        StringId description;

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
