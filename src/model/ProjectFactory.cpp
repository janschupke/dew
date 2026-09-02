#include "ProjectFactory.h"

#include "Ids.h"
#include "ProjectSchema.h"

namespace dew
{

namespace
{

juce::ValueTree makeChannel (int id, const juce::String& name, const juce::String& colour,
                             int basePitch, const juce::String& wave, int octave,
                             double attack, double decay, double sustain, double release,
                             double volume = 0.8)
{
    auto channel = defaultTreeFor (childSpecFor (projectSpec(), "channels"));

    channel.setProperty (ids::id, id, nullptr);
    channel.setProperty (ids::name, name, nullptr);
    channel.setProperty (ids::colour, colour, nullptr);
    channel.setProperty (ids::mixerTrackId, id, nullptr);
    channel.setProperty (ids::basePitch, basePitch, nullptr);
    channel.setProperty (ids::volume, volume, nullptr);

    auto instrument = channel.getChildWithName (ids::INSTRUMENT);
    auto osc = instrument.getChildWithName (ids::OSC);
    osc.setProperty (ids::wave, wave, nullptr);
    osc.setProperty (ids::octave, octave, nullptr);

    auto amp = instrument.getChildWithName (ids::AMP);
    amp.setProperty (ids::attack, attack, nullptr);
    amp.setProperty (ids::decay, decay, nullptr);
    amp.setProperty (ids::sustain, sustain, nullptr);
    amp.setProperty (ids::release, release, nullptr);

    return channel;
}

juce::ValueTree makeMixerTrack (int id, const juce::String& name)
{
    juce::ValueTree track (ids::MIXER_TRACK);
    track.setProperty (ids::id, id, nullptr);
    track.setProperty (ids::name, name, nullptr);
    track.setProperty (ids::gain, 0.8, nullptr);
    track.setProperty (ids::pan, 0.0, nullptr);
    track.setProperty (ids::mute, false, nullptr);
    track.setProperty (ids::solo, false, nullptr);
    return track;
}

juce::ValueTree makePlaylistTrack (const juce::String& name)
{
    juce::ValueTree track (ids::PLAYLIST_TRACK);
    track.setProperty (ids::name, name, nullptr);
    return track;
}

juce::ValueTree makeNote (int channelId, int step, int lengthSteps, int pitch, double velocity)
{
    juce::ValueTree note (ids::NOTE);
    note.setProperty (ids::ch, channelId, nullptr);
    note.setProperty (ids::step, step, nullptr);
    note.setProperty (ids::lengthSteps, lengthSteps, nullptr);
    note.setProperty (ids::pitch, pitch, nullptr);
    note.setProperty (ids::velocity, velocity, nullptr);
    return note;
}

juce::ValueTree makeClip (int patternId, int startBar, int lengthBars)
{
    juce::ValueTree clip (ids::CLIP);
    clip.setProperty (ids::patternId, patternId, nullptr);
    clip.setProperty (ids::startBar, startBar, nullptr);
    clip.setProperty (ids::lengthBars, lengthBars, nullptr);
    return clip;
}

const char* const kChannelColours[] = { "ffe4572e", "ff29a19c", "ff4fa3ff", "fff2c14e" };

} // namespace

juce::ValueTree ProjectFactory::createDefault()
{
    auto project = defaultTreeFor (projectSpec());
    project.setProperty (ids::name, "Untitled", nullptr);
    project.setProperty (ids::tempoBpm, 128.0, nullptr);

    // Kick and bass sit low; snare is noisy-ish via a fast square; lead sings.
    project.appendChild (makeChannel (1, "Kick",  kChannelColours[0], 36, "sine",     0,
                                      0.001, 0.140, 0.0, 0.060, 0.95), nullptr);
    project.appendChild (makeChannel (2, "Snare", kChannelColours[1], 60, "square",  -1,
                                      0.001, 0.090, 0.0, 0.060, 0.55), nullptr);
    project.appendChild (makeChannel (3, "Bass",  kChannelColours[2], 40, "saw",      0,
                                      0.004, 0.180, 0.35, 0.090, 0.75), nullptr);
    project.appendChild (makeChannel (4, "Lead",  kChannelColours[3], 72, "triangle", 0,
                                      0.006, 0.150, 0.55, 0.220, 0.60), nullptr);

    auto pattern = defaultTreeFor (childSpecFor (projectSpec(), "patterns"));
    pattern.setProperty (ids::id, 1, nullptr);
    pattern.setProperty (ids::name, "Pattern 1", nullptr);
    pattern.setProperty (ids::lengthSteps, 16, nullptr);
    project.appendChild (pattern, nullptr);

    auto playlist = project.getChildWithName (ids::PLAYLIST);
    for (int i = 1; i <= 4; ++i)
        playlist.appendChild (makePlaylistTrack ("Track " + juce::String (i)), nullptr);

    auto mixer = project.getChildWithName (ids::MIXER);
    for (int i = 1; i <= 4; ++i)
        mixer.appendChild (makeMixerTrack (i, "Insert " + juce::String (i)), nullptr);

    // Children were appended in construction order, not schema order; loading a
    // file always produces schema order, so normalise here or save-then-load
    // would reshape the tree.
    return canonicalTree (project, projectSpec());
}

juce::ValueTree ProjectFactory::createDemo()
{
    auto project = createDefault();
    project.setProperty (ids::name, "dew demo", nullptr);
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

    // A lead phrase with held notes, so release and sustain are audible.
    pattern.appendChild (makeNote (4, 0,  3, 72, 0.8), nullptr);
    pattern.appendChild (makeNote (4, 4,  2, 76, 0.75), nullptr);
    pattern.appendChild (makeNote (4, 8,  3, 79, 0.8), nullptr);
    pattern.appendChild (makeNote (4, 12, 4, 74, 0.7), nullptr);

    // One clip on the first playlist track, looping the pattern for four bars.
    auto playlist = project.getChildWithName (ids::PLAYLIST);
    playlist.getChild (0).appendChild (makeClip (1, 0, 4), nullptr);

    return canonicalTree (project, projectSpec());
}

} // namespace dew
