#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <vector>

namespace dew
{

/** What one segment of an automation curve does between its two points.

    Two shapes stored, three offered. A `line` is `curve` with a bend of zero,
    so storing it would be a second place holding the same fact, free to
    disagree with the bend beside it; the editor's menu ticks "Line" when the
    shape is `curve` and the bend is zero, which is derived rather than
    duplicated.
*/
enum class SegmentShape
{
    /** Interpolated, bent by the segment's own `curve` amount. A bend of zero
        is a straight line, which is what every curve written before shapes
        existed already was. */
    curve,

    /** Holds the LEFT value all the way across and jumps at the right point.

        Hold-left rather than hold-right, matching the way the evaluator already
        holds flat before the first point and after the last: a curve's value at
        a step is what the last point you passed said, until the next one.
    */
    step
};

/** Unknown text is `curve`, not a failure. A shape is a presentation choice on
    a segment that still has two endpoints and a bend, so a file naming one this
    build does not know still evaluates to something the author would recognise.
*/
SegmentShape segmentShapeFromString (const juce::String&);
juce::String segmentShapeToString (SegmentShape);

/** One point of a curve, with no ValueTree and no snapshot behind it.

    A plain value type because the two things that evaluate a curve live in
    different layers and one of them is the audio thread. Trivially copyable, so
    a snapshot holding a vector of these copies by the same rules as everything
    else in it.
*/
struct CurvePoint
{
    double step  = 0.0;
    double value = 0.0;   ///< 0..1, within the target's own range
    double curve = 0.0;   ///< -1..1 bend of the segment to the RIGHT of this point
    SegmentShape shape = SegmentShape::curve;   ///< of that same segment
};

/** The 0..1 value of a curve at a step. Held flat before the first point and
    after the last.

    THE evaluator. It had been written twice - once in ProjectEdits for the
    editor and the tests, once in EngineSnapshot for the audio thread - as two
    hand-copied bodies of the same arithmetic in two layers, and the painter was
    a third place that implemented neither and drew every segment straight. A
    bend was therefore heard and not seen, and a shape would have had to be
    added to three places that were already free to disagree.

    `points` must already be sorted by step: ProjectEdits keeps the tree sorted
    and buildSnapshot stable_sorts, so sorting a third time here would be a
    third opinion about the same fact.

    noexcept and allocation-free - the audio thread calls this every block.
*/
double curveValueAt (juce::Span<const CurvePoint> points, double step) noexcept;

/** The points of an AUTOMATION node, in the order the tree holds them.

    Message thread: it allocates. The audio thread reads the vector the snapshot
    built from this.
*/
std::vector<CurvePoint> curvePointsOf (const juce::ValueTree& automation);

} // namespace dew
