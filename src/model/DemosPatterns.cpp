// =============================================================================
// Getting Started, and the piano roll's demo.
//
// One of four translation units holding the demo library's CONTENT. The
// structure - the document File > New produces, and the table naming what
// ships - is ProjectFactory.cpp; the vocabulary these are written in is
// DemoBuilders.h; the loader that reads the committed .dew files rather than
// re-running any of this is DemoLibrary.h.
//
// The two that are about writing notes: a step grid under an arrangement,
// and chords, held notes and a velocity shape in the roll.
// =============================================================================

#include "model/ProjectFactory.h"

#include "model/DemoBuilders.h"
#include "model/Ids.h"
#include "model/ProjectSchema.h"

namespace dew
{

using namespace demo;

juce::ValueTree ProjectFactory::createDemo()
{
    auto project = createDefault();
    project.setProperty (ids::name, "dew demo", nullptr);
    project.setProperty (ids::tempoBpm, 124.0, nullptr);
    project.setProperty (ids::barsInSong, 16, nullptr);

    // Sixteen bars rather than four, and four patterns rather than one, because
    // a demo of a playlist that holds a single clip is a demo of a loop. Still
    // deliberately small: this is the first thing anybody opens, and every part
    // of it should be readable at a glance in the step grid it was written in.
    auto groove = patternIn (project, 1, "Groove", 16);
    auto intro = patternIn (project, 2, "Intro", 16);
    auto brk = patternIn (project, 3, "Break", 16);
    auto fill = patternIn (project, 4, "Fill", 16);

    // Kick on every beat.
    for (int step = 0; step < 16; step += 4)
        groove.appendChild (makeNote (1, step, 1, 36, 1.0), nullptr);

    // Snare on 2 and 4.
    for (int step = 4; step < 16; step += 8)
        groove.appendChild (makeNote (2, step, 1, 60, 0.9), nullptr);

    // Offbeat bass, root and fifth.
    const int bassPitches[] = { 40, 40, 47, 40, 40, 40, 45, 43 };
    for (int i = 0; i < 8; ++i)
        groove.appendChild (makeNote (3, i * 2 + 1, 1, bassPitches[i], 0.85), nullptr);

    // A lead phrase with held notes, so release and sustain are audible.
    groove.appendChild (makeNote (4, 0, 3, 72, 0.8), nullptr);
    groove.appendChild (makeNote (4, 4, 2, 76, 0.75), nullptr);
    groove.appendChild (makeNote (4, 8, 3, 79, 0.8), nullptr);
    groove.appendChild (makeNote (4, 12, 4, 74, 0.7), nullptr);

    // Intro: the pulse and the root, and nothing else yet.
    for (int step = 0; step < 16; step += 4)
        intro.appendChild (makeNote (1, step, 1, 36, 0.8), nullptr);

    for (int step = 0; step < 16; step += 8)
        intro.appendChild (makeNote (3, step, 6, 40, 0.7), nullptr);

    // Break: the kick drops out, which is what makes it a break. The lead is
    // the same phrase an octave up, so it reads as the same music, lifted.
    for (int step = 4; step < 16; step += 8)
        brk.appendChild (makeNote (2, step, 1, 60, 0.7), nullptr);

    brk.appendChild (makeNote (4, 0, 3, 84, 0.7), nullptr);
    brk.appendChild (makeNote (4, 4, 2, 88, 0.65), nullptr);
    brk.appendChild (makeNote (4, 8, 3, 91, 0.7), nullptr);
    brk.appendChild (makeNote (4, 12, 4, 86, 0.6), nullptr);

    // Fill: a snare roll that gets louder, landing back on the groove.
    for (int step = 0; step < 12; step += 4)
        fill.appendChild (makeNote (1, step, 1, 36, 1.0), nullptr);

    for (int i = 0; i < 6; ++i)
        fill.appendChild (makeNote (2, 10 + i, 1, 60, 0.55 + i * 0.08), nullptr);

    fill.appendChild (makeNote (3, 0, 8, 40, 0.85), nullptr);

    // The arrangement: in, groove, lift, groove, and a fill into whatever comes
    // next. Five clips on one lane, which is the smallest thing that is a song
    // rather than a loop.
    auto lane = project.getChildWithName (ids::PLAYLIST).getChild (0);
    lane.setProperty (ids::name, "Arrangement", nullptr);

    lane.appendChild (makeClip (2, 0, 4), nullptr);
    lane.appendChild (makeClip (1, 4, 4), nullptr);
    lane.appendChild (makeClip (3, 8, 2), nullptr);
    lane.appendChild (makeClip (1, 10, 5), nullptr);
    lane.appendChild (makeClip (4, 15, 1), nullptr);

    return canonicalTree (project, projectSpec());
}

juce::ValueTree ProjectFactory::createMelodyDemo()
{
    auto project = createDefault();
    project.setProperty (ids::name, "Piano Roll", nullptr);
    project.setProperty (ids::tempoBpm, 96.0, nullptr);
    project.setProperty (ids::barsInSong, 32, nullptr);

    // A softer set of voices than the default: this demo is about notes, not
    // drums. The melody is channel 1 deliberately - that is the channel the
    // piano roll opens on, and it should be the line the demo is about.
    auto melodyCh = channelWithId (project, 1);
    auto chordsCh = channelWithId (project, 2);
    auto bassCh = channelWithId (project, 3);
    auto counterCh = channelWithId (project, 4);

    melodyCh.setProperty (ids::name, "Melody", nullptr);
    setClassicOsc (melodyCh, 0, "saw", 0, 0.8);
    setAmp (melodyCh, 0.005, 0.120, 0.700, 0.300);

    chordsCh.setProperty (ids::name, "Chords", nullptr);
    setClassicOsc (chordsCh, 0, "triangle", 0, 0.8);
    setAmp (chordsCh, 0.020, 0.120, 0.700, 0.450);

    bassCh.setProperty (ids::name, "Bass", nullptr);
    setAmp (bassCh, 0.005, 0.120, 0.700, 0.250);

    counterCh.setProperty (ids::name, "Counter", nullptr);
    counterCh.setProperty (ids::volume, 0.4, nullptr);
    setClassicOsc (counterCh, 0, "sine", 0, 0.8);

    // Three four-bar sections, each a pattern. The A section is the one the
    // demo has always had - it is what the piano roll opens on, and the test
    // that checks this demo for chords and varied note lengths reads it.
    struct Section
    {
        int id;
        const char* name;
        int roots[4];
        int triads[4][3];
        int melody[14][4]; // step, length, pitch, velocity per cent; pitch 0 ends it
        int counterTop;
    };

    static const Section sections[] {
        { 1,
          "Am - F - C - G",
          { 45, 41, 48, 43 },
          { { 57, 60, 64 }, { 53, 57, 60 }, { 60, 64, 67 }, { 55, 59, 62 } },
          { { 0, 3, 76, 95 },
            { 4, 2, 74, 70 },
            { 6, 2, 72, 60 },
            { 8, 6, 69, 85 },
            { 16, 3, 72, 90 },
            { 20, 2, 74, 65 },
            { 22, 2, 76, 75 },
            { 24, 6, 77, 95 },
            { 32, 4, 79, 100 },
            { 38, 2, 76, 60 },
            { 40, 6, 74, 80 },
            { 48, 3, 71, 85 },
            { 52, 3, 74, 70 },
            { 56, 8, 69, 90 } },
          88 },

        { 2,
          "Dm - Bb - F - C",
          { 50, 46, 53, 48 },
          { { 62, 65, 69 }, { 58, 62, 65 }, { 57, 60, 65 }, { 60, 64, 67 } },
          { { 0, 4, 81, 90 },
            { 6, 2, 79, 65 },
            { 8, 6, 77, 85 },
            { 16, 3, 74, 85 },
            { 20, 2, 77, 70 },
            { 22, 2, 79, 80 },
            { 24, 6, 81, 95 },
            { 32, 5, 77, 90 },
            { 38, 3, 74, 65 },
            { 42, 4, 72, 75 },
            { 48, 4, 76, 85 },
            { 54, 2, 74, 60 },
            { 56, 8, 72, 85 },
            { 0, 0, 0, 0 } },
          93 },

        { 3,
          "Bridge",
          { 43, 43, 41, 41 },
          { { 55, 59, 62 }, { 55, 62, 67 }, { 53, 57, 60 }, { 53, 60, 65 } },
          { { 8, 8, 67, 70 },
            { 24, 8, 71, 75 },
            { 40, 8, 74, 80 },
            { 56, 8, 76, 85 },
            { 0, 0, 0, 0 } },
          0 },
    };

    for (const auto& section : sections)
    {
        auto pattern = patternIn (project, section.id, section.name, 64);

        for (int bar = 0; bar < 4; ++bar)
        {
            const auto start = bar * 16;

            // Bass: root on the downbeat, held, then an octave lift.
            pattern.appendChild (makeNote (3, start, 6, section.roots[bar], 0.9), nullptr);
            pattern.appendChild (makeNote (3, start + 8, 4, section.roots[bar] + 12, 0.55),
                                 nullptr);

            // Chords: three notes at once, which is the thing a step grid cannot do.
            for (const auto pitch : section.triads[bar])
                pattern.appendChild (makeNote (2, start + 2, 12, pitch, 0.5), nullptr);
        }

        // A melody with real note lengths and a velocity shape, so the piano
        // roll's velocity lane has something in it worth looking at.
        for (const auto& note : section.melody)
        {
            if (note[2] == 0)
                break;

            pattern.appendChild (makeNote (1, note[0], note[1], note[2], note[3] / 100.0), nullptr);
        }

        // A quiet counter-line an octave up, off the beat. The bridge has none:
        // taking a voice away is as much a section as adding one.
        if (section.counterTop > 0)
            for (int step = 2; step < 64; step += 8)
                pattern.appendChild (
                    makeNote (4, step, 1, section.counterTop - (step / 16) * 2, 0.35), nullptr);
    }

    // A A' B A B' bridge A - the shape of most songs anybody has ever hummed,
    // and thirty-two bars of somewhere to go rather than four bars repeated.
    auto lane = project.getChildWithName (ids::PLAYLIST).getChild (0);
    lane.setProperty (ids::name, "Arrangement", nullptr);

    const int arrangement[][2] = {
        { 1, 0 }, { 1, 4 }, { 2, 8 }, { 1, 12 }, { 3, 16 }, { 2, 20 }, { 1, 24 }, { 3, 28 },
    };

    for (const auto& clip : arrangement)
        lane.appendChild (makeClip (clip[0], clip[1], 4), nullptr);

    return canonicalTree (project, projectSpec());
}

} // namespace dew
