
#include "model/Ids.h"
#include "model/ProjectFactory.h"
#include "model/ProjectSchema.h"
#include "FixtureProject.h"

namespace dew::testing
{

namespace
{

/** A note, built from the schema rather than by hand.

    ProjectFactory has a helper of the same shape in its anonymous namespace and
    this does not reach for it: a fixture that borrowed the factory's private
    parts would be coupled to the very thing it exists to be independent of. It
    is six lines, and defaultTreeFor is public precisely so a caller can build a
    canonically-shaped node without one.
*/
juce::ValueTree makeNote (int channelId, int step, int lengthSteps, int pitch, double velocity)
{
    auto note = defaultTreeFor (childSpecFor (childSpecFor (projectSpec(), "patterns"), "notes"));
    note.setProperty (ids::ch, channelId, nullptr);
    note.setProperty (ids::step, step, nullptr);
    note.setProperty (ids::lengthSteps, lengthSteps, nullptr);
    note.setProperty (ids::pitch, pitch, nullptr);
    note.setProperty (ids::velocity, velocity, nullptr);
    return note;
}

juce::ValueTree makeClip (int patternId, int startBar, int lengthBars)
{
    auto clip = defaultTreeFor (childSpecFor (childSpecFor (childSpecFor (projectSpec(), "playlist"),
                                                            "tracks"),
                                              "clips"));
    clip.setProperty (ids::kind, "pattern", nullptr);
    clip.setProperty (ids::patternId, patternId, nullptr);
    clip.setProperty (ids::startBar, startBar, nullptr);
    clip.setProperty (ids::lengthBars, lengthBars, nullptr);
    return clip;
}

} // namespace

juce::ValueTree fixtureProject()
{
    auto project = ProjectFactory::createDefault();
    project.setProperty (ids::name, "dew fixture", nullptr);
    project.setProperty (ids::tempoBpm, 124.0, nullptr);
    project.setProperty (ids::barsInSong, 4, nullptr);

    auto pattern = project.getChildWithName (ids::PATTERN);
    pattern.setProperty (ids::name, "Groove", nullptr);

    // Kick on every beat.
    for (int step = 0; step < 16; step += 4)
        pattern.appendChild (makeNote (1, step, 1, 36, 1.0), nullptr);

    // Snare on 2 and 4.
    for (int step = 4; step < 16; step += 8)
        pattern.appendChild (makeNote (2, step, 1, 60, 0.9), nullptr);

    // Offbeat bass, root and fifth.
    const int bassPitches[] = { 40, 40, 47, 40, 40, 40, 45, 43 };
    for (int i = 0; i < 8; ++i)
        pattern.appendChild (makeNote (3, i * 2 + 1, 1, bassPitches[i], 0.85), nullptr);

    // A lead phrase with held notes, so release and sustain are audible - and
    // so the four distinct note lengths the piano roll tests want are here.
    pattern.appendChild (makeNote (4, 0,  3, 72, 0.8), nullptr);
    pattern.appendChild (makeNote (4, 4,  2, 76, 0.75), nullptr);
    pattern.appendChild (makeNote (4, 8,  3, 79, 0.8), nullptr);
    pattern.appendChild (makeNote (4, 12, 4, 74, 0.7), nullptr);

    // One clip on the first playlist track, looping the pattern for four bars.
    auto playlist = project.getChildWithName (ids::PLAYLIST);
    playlist.getChild (0).appendChild (makeClip (1, 0, 4), nullptr);

    // Children were appended in construction order, not schema order; loading a
    // file always produces schema order, so normalise here or a fixture that
    // went through save-then-load would not compare equal to this one.
    return canonicalTree (project, projectSpec());
}

} // namespace dew::testing
