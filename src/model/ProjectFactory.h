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

    /** The demo library: ten tracks, each a real piece of music in a real genre,
        which between them use what the app can do because the music asks for it.

        Shipped as files under examples/ and embedded in the binary, so the
        Demos menu works from a build with no source tree beside it.

        They are named after the tracks rather than after features on purpose.
        A library named Piano Roll, Effect Chain, Automation reads as a checklist
        and demonstrates each thing exactly once, which is how the FM matrix, the
        LFO, four of the ten effects, both non-synth instruments, three
        automation scopes and every metre but 4/4 came to be shipped with nothing
        that used them. What holds the coverage now is not a demo per feature but
        a test over the whole library - see tests/DemoLibraryTests.cpp.
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

        juce::ValueTree (*build)();
    };

    static const std::vector<Demo>& demos();

    /** The first demo in the library.

        What a screenshot, the offline-render smoke test and the engine's own
        pin open when they want "a project with music in it" and do not care
        which. Reads the table rather than being an eleventh builder beside it:
        there used to be a `createDemo` that was both the first entry AND its own
        function, which is one fact with two homes.
    */
    static juce::ValueTree createDemo();

    /** Techno. Four drum channels on one insert, a pumping curve on the two
        beside it, and a master chain that ends in a limiter. */
    static juce::ValueTree createPulseCode();

    /** UK garage. A swung grid - twelve steps to a beat - and a hat that
        breathes under an LFO locked to the tempo. */
    static juce::ValueTree createCopperWire();

    /** Ambient in 6/8, from examples/halcyon.score. Two wavetable pads, one
        morphing under its envelope and one under a free-running LFO. */
    static juce::ValueTree createHalcyon();

    /** A neo-soul ballad, from examples/rhodes.score. The FM matrix used the way
        an electric piano uses it: a sine folded into a sine. */
    static juce::ValueTree createLateRhodes();

    /** Math rock in 7/8, from examples/ironmeter.score - with a kit written
        here, because a score owns harmony and a drum part is placement. */
    static juce::ValueTree createIronMeter();

    /** A waltz in 3/4, from examples/nightfall.score. Counterpoint, a canon, and
        a ritardando drawn as a tempo curve. */
    static juce::ValueTree createNightfallWaltz();

    /** Drum and bass. A Reese bass built out of FM feedback on the matrix
        diagonal, swept by a bandpass. */
    static juce::ValueTree createSublevel();

    /** Synthwave. A wavetable position that STEPS between two frames rather than
        sliding, and one muted lane carrying a spare idea. */
    static juce::ValueTree createChromeCoast();

    /** Half-time trap at 140. Thirty-second hi-hat rolls, so eight steps to a
        beat, and a riser that lands off the bar line. */
    static juce::ValueTree createSlowGrain();

    /** Berlin-school sequencer music in 5/4. FM bells, a synced vibrato, and a
        delay whose feedback climbs on a curve. */
    static juce::ValueTree createTidalLock();
};

} // namespace dew
