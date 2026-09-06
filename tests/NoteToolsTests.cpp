#include <catch2/catch_test_macros.hpp>

#include "model/Ids.h"
#include "model/NoteTools.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"

using namespace dew;

namespace
{

/** A project with one empty pattern and an undo manager, which is all any of
    these need - none of them touch the engine or a component.
*/
struct ToolsFixture
{
    ToolsFixture()
        : project (ProjectFactory::createDefault())
        , pattern (ProjectEdits::findPattern (project, 1))
    {
    }

    juce::ValueTree add (int step, int length, int pitch, float velocity = 1.0f, int channelId = 1)
    {
        return ProjectEdits::addNote (pattern, channelId, step, length, pitch, velocity, &undo);
    }

    int countNotes() const
    {
        int count = 0;

        for (const auto& child : pattern)
            if (child.hasType (ids::NOTE))
                ++count;

        return count;
    }

    juce::ValueTree project;
    juce::ValueTree pattern;
    juce::UndoManager undo;
};

juce::Array<juce::ValueTree> listOf (std::initializer_list<juce::ValueTree> notes)
{
    juce::Array<juce::ValueTree> array;

    for (const auto& note : notes)
        array.add (note);

    return array;
}

/** The default project's metre, which is what every fixture here uses. The two
    operations that can change a pattern's extent take it, because a pattern's
    length is measured in whole bars - see ProjectEdits::fitPatternToNotes. */
constexpr int stepsPerBar = 16;

} // namespace

// --- snapping ----------------------------------------------------------------

TEST_CASE ("snap divisions resolve against the project's own resolution", "[model][notetools]")
{
    // The default: a step is a sixteenth, so the finest division is one step.
    CHECK (NoteTools::stepsForSnap (SnapDivision::sixteenth, 4) == 1);
    CHECK (NoteTools::stepsForSnap (SnapDivision::eighth, 4) == 2);
    CHECK (NoteTools::stepsForSnap (SnapDivision::quarter, 4) == 4);
    CHECK (NoteTools::stepsForSnap (SnapDivision::half, 4) == 8);
    CHECK (NoteTools::stepsForSnap (SnapDivision::bar, 4) == 16);

    // A project at another resolution gets a grid that still means what it says,
    // rather than a dropdown labelled 1/4 that snaps to something else.
    CHECK (NoteTools::stepsForSnap (SnapDivision::quarter, 3) == 3);
    CHECK (NoteTools::stepsForSnap (SnapDivision::bar, 3) == 12);

    // Never zero, whatever the project claims - every snap divides by this.
    for (const auto division : NoteTools::allSnapDivisions)
    {
        CHECK (NoteTools::stepsForSnap (division, 1) >= 1);
        CHECK (NoteTools::stepsForSnap (division, 0) >= 1);
    }
}

TEST_CASE ("a snap index survives a round trip and a bad one falls back", "[model][notetools]")
{
    for (const auto division : NoteTools::allSnapDivisions)
        CHECK (NoteTools::snapFromIndex (NoteTools::indexOfSnap (division)) == division);

    CHECK (NoteTools::snapFromIndex (-1) == SnapDivision::sixteenth);
    CHECK (NoteTools::snapFromIndex (99) == SnapDivision::sixteenth);
}

TEST_CASE ("floor, nearest and ceiling never move a step onto the wrong side", "[model][notetools]")
{
    CHECK (NoteTools::snapFloor (0, 4) == 0);
    CHECK (NoteTools::snapFloor (3, 4) == 0);
    CHECK (NoteTools::snapFloor (4, 4) == 4);
    CHECK (NoteTools::snapFloor (7, 4) == 4);

    CHECK (NoteTools::snapNearest (0, 4) == 0);
    CHECK (NoteTools::snapNearest (1, 4) == 0);
    CHECK (NoteTools::snapNearest (2, 4) == 4);
    CHECK (NoteTools::snapNearest (6, 4) == 8);

    CHECK (NoteTools::snapCeil (1, 4) == 4);
    CHECK (NoteTools::snapCeil (4, 4) == 4);
    CHECK (NoteTools::snapCeil (5, 4) == 8);

    // At the finest grid every one of them is the identity, which is why the
    // existing piano roll behaviour is unchanged at the default division.
    for (int step = 0; step < 8; ++step)
    {
        CHECK (NoteTools::snapFloor (step, 1) == step);
        CHECK (NoteTools::snapNearest (step, 1) == step);
        CHECK (NoteTools::snapCeil (step, 1) == step);
    }
}

// --- scope -------------------------------------------------------------------

TEST_CASE ("the scope is the selection when there is one, and the channel when there is not",
           "[model][notetools]")
{
    ToolsFixture f;

    const auto first = f.add (0, 1, 60);
    const auto second = f.add (4, 1, 62);
    f.add (8, 1, 64);
    f.add (0, 1, 40, 1.0f, 2); // another channel

    CHECK (NoteTools::scopeFor (f.pattern, 1, {}).size() == 3);
    CHECK (NoteTools::scopeFor (f.pattern, 2, {}).size() == 1);

    const auto selected = listOf ({ first, second });
    CHECK (NoteTools::scopeFor (f.pattern, 1, selected).size() == 2);

    // The channel filter is not bypassed by an empty selection on a channel with
    // no notes: an empty scope is a no-op, not "everything".
    CHECK (NoteTools::scopeFor (f.pattern, 3, {}).isEmpty());
}

TEST_CASE ("a covering note is found from inside it, not only at its start", "[model][notetools]")
{
    ToolsFixture f;
    f.add (4, 4, 60);

    CHECK (NoteTools::noteCovering (f.pattern, 1, 4, 60).isValid());
    CHECK (NoteTools::noteCovering (f.pattern, 1, 6, 60).isValid());
    CHECK (! NoteTools::noteCovering (f.pattern, 1, 8, 60).isValid()); // one past the end
    CHECK (! NoteTools::noteCovering (f.pattern, 1, 6, 61).isValid()); // wrong pitch
    CHECK (! NoteTools::noteCovering (f.pattern, 2, 6, 60).isValid()); // wrong channel
}

// --- slice -------------------------------------------------------------------

TEST_CASE ("slicing produces two notes that together cover the original", "[model][notetools]")
{
    ToolsFixture f;
    auto note = f.add (4, 8, 60, 0.7f);

    auto tail = NoteTools::sliceNote (f.pattern, note, 6, &f.undo);

    REQUIRE (tail.isValid());
    CHECK (f.countNotes() == 2);

    CHECK ((int) note[ids::step] == 4);
    CHECK ((int) note[ids::lengthSteps] == 2);
    CHECK ((int) tail[ids::step] == 6);
    CHECK ((int) tail[ids::lengthSteps] == 6);

    // Pitch and velocity travel with the tail: a slice is a rhythmic edit.
    CHECK ((int) tail[ids::pitch] == 60);
    CHECK (juce::exactlyEqual ((double) tail[ids::velocity], (double) note[ids::velocity]));
}

TEST_CASE ("a one-step note is not sliced", "[model][notetools]")
{
    ToolsFixture f;
    auto note = f.add (4, 1, 60);

    CHECK (! NoteTools::sliceNote (f.pattern, note, 4, &f.undo).isValid());
    CHECK (f.countNotes() == 1);
    CHECK ((int) note[ids::lengthSteps] == 1);
}

TEST_CASE ("a cut on or outside a note's own bounds does nothing", "[model][notetools]")
{
    ToolsFixture f;
    auto note = f.add (4, 4, 60);

    for (const auto cut : { 0, 4, 8, 12 })
    {
        CHECK (! NoteTools::sliceNote (f.pattern, note, cut, &f.undo).isValid());
        CHECK (f.countNotes() == 1);
        CHECK ((int) note[ids::lengthSteps] == 4);
    }
}

TEST_CASE ("slicing a run of notes is one undo step", "[model][notetools]")
{
    ToolsFixture f;
    auto a = f.add (0, 8, 60);
    auto b = f.add (0, 8, 62);
    auto c = f.add (0, 8, 64);

    f.undo.beginNewTransaction ("Slice notes");

    for (auto note : { a, b, c })
        NoteTools::sliceNote (f.pattern, note, 4, &f.undo);

    REQUIRE (f.countNotes() == 6);

    REQUIRE (f.undo.undo());

    CHECK (f.countNotes() == 3);
    CHECK ((int) a[ids::lengthSteps] == 8);
    CHECK ((int) b[ids::lengthSteps] == 8);
    CHECK ((int) c[ids::lengthSteps] == 8);
}

// --- quantize ----------------------------------------------------------------

TEST_CASE ("quantize rounds starts to the nearest line and leaves lengths alone",
           "[model][notetools]")
{
    ToolsFixture f;
    auto a = f.add (1, 3, 60);
    auto b = f.add (3, 5, 62);
    auto c = f.add (5, 2, 64);
    auto d = f.add (7, 1, 65);

    CHECK (NoteTools::quantize (f.pattern, NoteTools::scopeFor (f.pattern, 1, {}), 4, stepsPerBar,
                                &f.undo)
           == 0);

    CHECK ((int) a[ids::step] == 0);
    CHECK ((int) b[ids::step] == 4);
    CHECK ((int) c[ids::step] == 4);
    CHECK ((int) d[ids::step] == 8);

    CHECK ((int) a[ids::lengthSteps] == 3);
    CHECK ((int) b[ids::lengthSteps] == 5);
    CHECK ((int) c[ids::lengthSteps] == 2);
    CHECK ((int) d[ids::lengthSteps] == 1);
}

TEST_CASE ("quantizing an already-quantized pattern changes nothing", "[model][notetools]")
{
    ToolsFixture f;
    f.add (0, 4, 60);
    f.add (4, 4, 62);
    f.add (8, 4, 64);

    const auto scope = NoteTools::scopeFor (f.pattern, 1, {});

    CHECK (NoteTools::quantize (f.pattern, scope, 4, stepsPerBar, &f.undo) == 0);
    CHECK (NoteTools::quantize (f.pattern, scope, 4, stepsPerBar, &f.undo) == 0);
    CHECK (f.countNotes() == 3);
}

TEST_CASE ("quantize collapses notes landing on one step and pitch, keeping the longer",
           "[model][notetools]")
{
    ToolsFixture f;
    auto shorter = f.add (3, 1, 60);
    auto longer = f.add (5, 4, 60);

    CHECK (NoteTools::quantize (f.pattern, NoteTools::scopeFor (f.pattern, 1, {}), 4, stepsPerBar,
                                &f.undo)
           == 1);

    CHECK (f.countNotes() == 1);
    CHECK ((int) shorter[ids::step] == 4);
    CHECK ((int) shorter[ids::lengthSteps] == 4);

    // The survivor is the first in tree order, grown to the longer duration.
    CHECK (f.pattern.indexOf (longer) < 0);
}

TEST_CASE ("quantize does not collapse across pitches or channels", "[model][notetools]")
{
    ToolsFixture f;
    f.add (3, 1, 60);
    f.add (5, 1, 62);          // same step after quantizing, different pitch
    f.add (5, 1, 60, 1.0f, 2); // same step and pitch, different channel

    auto scope = NoteTools::notesOnChannel (f.pattern, 1);
    scope.addArray (NoteTools::notesOnChannel (f.pattern, 2));

    CHECK (NoteTools::quantize (f.pattern, scope, 4, stepsPerBar, &f.undo) == 0);
    CHECK (f.countNotes() == 3);
}

TEST_CASE ("quantize grows the pattern when it pushes a note past the end", "[model][notetools]")
{
    ToolsFixture f;
    const auto originalLength = (int) f.pattern[ids::lengthSteps];

    f.add (originalLength - 1, 1, 60);

    NoteTools::quantize (f.pattern, NoteTools::scopeFor (f.pattern, 1, {}), 16, stepsPerBar,
                         &f.undo);

    CHECK ((int) f.pattern[ids::lengthSteps] > originalLength);
}

TEST_CASE ("a quantize is one undo step", "[model][notetools]")
{
    ToolsFixture f;
    auto a = f.add (1, 1, 60);
    auto b = f.add (7, 1, 62);

    f.undo.beginNewTransaction ("Quantize");
    NoteTools::quantize (f.pattern, NoteTools::scopeFor (f.pattern, 1, {}), 4, stepsPerBar,
                         &f.undo);

    REQUIRE (f.undo.undo());

    CHECK ((int) a[ids::step] == 1);
    CHECK ((int) b[ids::step] == 7);
}

// --- transpose ---------------------------------------------------------------

TEST_CASE ("transpose moves a group by one interval", "[model][notetools]")
{
    ToolsFixture f;
    auto a = f.add (0, 1, 60);
    auto b = f.add (0, 1, 64);
    auto c = f.add (0, 1, 67);

    CHECK (NoteTools::transpose (NoteTools::scopeFor (f.pattern, 1, {}), 12, 12, 108, &f.undo)
           == 12);

    CHECK ((int) a[ids::pitch] == 72);
    CHECK ((int) b[ids::pitch] == 76);
    CHECK ((int) c[ids::pitch] == 79);
}

TEST_CASE ("a chord against the ceiling keeps its intervals instead of compressing",
           "[model][notetools]")
{
    ToolsFixture f;
    auto low = f.add (0, 1, 100);
    auto high = f.add (0, 1, 108); // already at the top of the roll

    // Nothing can move, so nothing does - and the caller is told, so it can skip
    // an undo transaction that would restore nothing.
    CHECK (NoteTools::transpose (NoteTools::scopeFor (f.pattern, 1, {}), 1, 12, 108, &f.undo) == 0);
    CHECK ((int) low[ids::pitch] == 100);
    CHECK ((int) high[ids::pitch] == 108);

    // Down is still available, and the interval is preserved.
    CHECK (NoteTools::transpose (NoteTools::scopeFor (f.pattern, 1, {}), -12, 12, 108, &f.undo)
           == -12);
    CHECK ((int) high[ids::pitch] - (int) low[ids::pitch] == 8);
}

TEST_CASE ("transposing by nothing is a no-op", "[model][notetools]")
{
    ToolsFixture f;
    f.add (0, 1, 60);

    CHECK (NoteTools::transpose (NoteTools::scopeFor (f.pattern, 1, {}), 0, 12, 108, &f.undo) == 0);
    CHECK (NoteTools::transpose ({}, 12, 12, 108, &f.undo) == 0);
}

// --- randomize ---------------------------------------------------------------

TEST_CASE ("randomize with a fixed seed is reproducible and stays in range", "[model][notetools]")
{
    const NoteTools::RandomizeOptions options { 0.4, 2 };

    const auto run = [&options] (juce::Array<int>& steps, juce::Array<double>& velocities)
    {
        ToolsFixture f;

        for (int i = 0; i < 40; ++i)
            f.add (i * 2, 1, 60, 0.6f);

        juce::Random random (1234);
        NoteTools::randomize (f.pattern, NoteTools::scopeFor (f.pattern, 1, {}), options, random,
                              stepsPerBar, &f.undo);

        for (const auto& note : NoteTools::notesOnChannel (f.pattern, 1))
        {
            steps.add ((int) note[ids::step]);
            velocities.add ((double) note[ids::velocity]);
        }
    };

    juce::Array<int> stepsA, stepsB;
    juce::Array<double> velocitiesA, velocitiesB;

    run (stepsA, velocitiesA);
    run (stepsB, velocitiesB);

    CHECK (stepsA == stepsB);

    REQUIRE (velocitiesA.size() == velocitiesB.size());

    for (int i = 0; i < velocitiesA.size(); ++i)
        CHECK (juce::exactlyEqual (velocitiesA[i], velocitiesB[i]));

    for (const auto velocity : velocitiesA)
    {
        CHECK (velocity >= 0.05);
        CHECK (velocity <= 1.0);
    }

    for (const auto step : stepsA)
        CHECK (step >= 0);
}

TEST_CASE ("randomize touches only the properties it was asked to", "[model][notetools]")
{
    ToolsFixture f;
    auto note = f.add (8, 3, 60, 0.6f);

    juce::Random random (7);
    NoteTools::randomize (f.pattern, NoteTools::scopeFor (f.pattern, 1, {}), { 0.4, 0 }, random,
                          stepsPerBar, &f.undo);

    CHECK ((int) note[ids::step] == 8);
    CHECK ((int) note[ids::lengthSteps] == 3);
    CHECK ((int) note[ids::pitch] == 60);

    const auto velocityAfter = (double) note[ids::velocity];

    NoteTools::randomize (f.pattern, NoteTools::scopeFor (f.pattern, 1, {}), { 0.0, 2 }, random,
                          stepsPerBar, &f.undo);

    CHECK (juce::exactlyEqual ((double) note[ids::velocity], velocityAfter));
    CHECK ((int) note[ids::pitch] == 60);
    CHECK ((int) note[ids::lengthSteps] == 3);
}

TEST_CASE ("randomize with both amounts at zero writes nothing at all", "[model][notetools]")
{
    ToolsFixture f;
    auto note = f.add (4, 2, 60, 0.6f);

    f.undo.beginNewTransaction ("Randomize");

    juce::Random random (1);
    NoteTools::randomize (f.pattern, NoteTools::scopeFor (f.pattern, 1, {}), { 0.0, 0 }, random,
                          stepsPerBar, &f.undo);

    CHECK ((int) note[ids::step] == 4);

    // addNote widens a float, so the stored value is 0.6f promoted - not the
    // double literal 0.6, which is a different number.
    CHECK (juce::exactlyEqual ((double) note[ids::velocity], (double) 0.6f));

    // Nothing was written, so the transaction that was opened for it is empty.
    CHECK (f.undo.getNumActionsInCurrentTransaction() == 0);
}

TEST_CASE ("randomize clamps a note pushed before the start rather than losing it",
           "[model][notetools]")
{
    ToolsFixture f;

    for (int i = 0; i < 20; ++i)
        f.add (0, 1, 60 + i);

    juce::Random random (99);

    for (int pass = 0; pass < 10; ++pass)
        NoteTools::randomize (f.pattern, NoteTools::scopeFor (f.pattern, 1, {}), { 0.0, 4 }, random,
                              stepsPerBar, &f.undo);

    CHECK (f.countNotes() == 20);

    for (const auto& note : NoteTools::notesOnChannel (f.pattern, 1))
        CHECK ((int) note[ids::step] >= 0);
}
