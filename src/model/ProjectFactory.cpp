#include "ProjectFactory.h"

#include "Ids.h"
#include "ProjectEdits.h"
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
    auto osc = ProjectEdits::oscillatorAt (channel, 0);
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
    auto track = defaultTreeFor (childSpecFor (childSpecFor (projectSpec(), "mixer"), "tracks"));
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
    auto track = defaultTreeFor (childSpecFor (childSpecFor (projectSpec(), "playlist"), "tracks"));
    track.setProperty (ids::name, name, nullptr);
    return track;
}

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

const NodeSpec& clipSpec()
{
    return childSpecFor (childSpecFor (childSpecFor (projectSpec(), "playlist"), "tracks"), "clips");
}

juce::ValueTree makeClip (int patternId, int startBar, int lengthBars)
{
    auto clip = defaultTreeFor (clipSpec());
    clip.setProperty (ids::kind, "pattern", nullptr);
    clip.setProperty (ids::patternId, patternId, nullptr);
    clip.setProperty (ids::startBar, startBar, nullptr);
    clip.setProperty (ids::lengthBars, lengthBars, nullptr);
    return clip;
}

juce::ValueTree makeAutomationClip (int automationId, int startBar, int lengthBars)
{
    auto clip = defaultTreeFor (clipSpec());
    clip.setProperty (ids::kind, "automation", nullptr);
    clip.setProperty (ids::automationId, automationId, nullptr);
    clip.setProperty (ids::startBar, startBar, nullptr);
    clip.setProperty (ids::lengthBars, lengthBars, nullptr);
    return clip;
}

juce::ValueTree makeEffect (int id, const juce::String& type,
                            std::initializer_list<std::pair<const juce::Identifier&, double>> params)
{
    auto effect = defaultTreeFor (childSpecFor (childSpecFor (projectSpec(), "channels"), "effects"));
    effect.setProperty (ids::id, id, nullptr);
    effect.setProperty (ids::type, type, nullptr);

    for (const auto& [property, value] : params)
        effect.setProperty (property, value, nullptr);

    return effect;
}

juce::ValueTree makeAutomation (int id, const juce::String& name, const juce::String& scope,
                                int targetId, int slot, const juce::Identifier& param,
                                std::initializer_list<std::pair<double, double>> points)
{
    auto automation = defaultTreeFor (childSpecFor (projectSpec(), "automations"));
    automation.setProperty (ids::id, id, nullptr);
    automation.setProperty (ids::name, name, nullptr);
    automation.setProperty (ids::scope, scope, nullptr);
    automation.setProperty (ids::targetId, targetId, nullptr);
    automation.setProperty (ids::slot, slot, nullptr);
    automation.setProperty (ids::param, param.toString(), nullptr);

    for (const auto& [step, value] : points)
    {
        auto point = defaultTreeFor (childSpecFor (childSpecFor (projectSpec(), "automations"), "points"));
        point.setProperty (ids::step, step, nullptr);
        point.setProperty (ids::value, value, nullptr);
        automation.appendChild (point, nullptr);
    }

    return automation;
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

juce::ValueTree ProjectFactory::createMelodyDemo()
{
    auto project = createDefault();
    project.setProperty (ids::name, "Piano Roll", nullptr);
    project.setProperty (ids::tempoBpm, 96.0, nullptr);
    project.setProperty (ids::barsInSong, 4, nullptr);

    // A softer set of voices than the default: this demo is about notes, not
    // drums. The melody is channel 1 deliberately - that is the channel the
    // piano roll opens on, and it should be the line the demo is about.
    int index = 0;

    for (auto channel : project)
    {
        if (! channel.hasType (ids::CHANNEL))
            continue;

        auto amp = channel.getChildWithName (ids::INSTRUMENT).getChildWithName (ids::AMP);

        if (index == 0) { channel.setProperty (ids::name, "Melody", nullptr); amp.setProperty (ids::release, 0.30, nullptr);
                          ProjectEdits::oscillatorAt (channel, 0)
                                       .setProperty (ids::wave, "saw", nullptr); }
        if (index == 1) { channel.setProperty (ids::name, "Chords", nullptr); amp.setProperty (ids::attack, 0.02, nullptr);
                          amp.setProperty (ids::sustain, 0.7, nullptr); amp.setProperty (ids::release, 0.45, nullptr);
                          ProjectEdits::oscillatorAt (channel, 0)
                                       .setProperty (ids::wave, "triangle", nullptr); }
        if (index == 2) { channel.setProperty (ids::name, "Bass", nullptr); amp.setProperty (ids::release, 0.25, nullptr); }
        if (index == 3) { channel.setProperty (ids::name, "Counter", nullptr); channel.setProperty (ids::volume, 0.4, nullptr);
                          ProjectEdits::oscillatorAt (channel, 0)
                                       .setProperty (ids::wave, "sine", nullptr); }

        ++index;
    }

    auto pattern = project.getChildWithName (ids::PATTERN);
    pattern.setProperty (ids::name, "Am - F - C - G", nullptr);
    pattern.setProperty (ids::lengthSteps, 64, nullptr);

    // Four bars of i - VI - III - VII in A minor, one chord a bar.
    const int roots[] = { 45, 41, 48, 43 };
    const int triads[][3] = { { 57, 60, 64 }, { 53, 57, 60 }, { 60, 64, 67 }, { 55, 59, 62 } };

    for (int bar = 0; bar < 4; ++bar)
    {
        const auto start = bar * 16;

        // Bass: root on the downbeat, held, then an octave lift.
        pattern.appendChild (makeNote (3, start, 6, roots[bar], 0.9), nullptr);
        pattern.appendChild (makeNote (3, start + 8, 4, roots[bar] + 12, 0.55), nullptr);

        // Chords: three notes at once, which is the thing a step grid cannot do.
        for (const auto pitch : triads[bar])
            pattern.appendChild (makeNote (2, start + 2, 12, pitch, 0.5), nullptr);
    }

    // A melody with real note lengths and a velocity shape, so the piano roll's
    // velocity lane has something in it worth looking at.
    const int melody[][4] = {
        //  step, length, pitch, velocity as a percentage
        {  0, 3, 76,  95 }, {  4, 2, 74,  70 }, {  6, 2, 72,  60 }, {  8, 6, 69,  85 },
        { 16, 3, 72,  90 }, { 20, 2, 74,  65 }, { 22, 2, 76,  75 }, { 24, 6, 77,  95 },
        { 32, 4, 79, 100 }, { 38, 2, 76,  60 }, { 40, 6, 74,  80 },
        { 48, 3, 71,  85 }, { 52, 3, 74,  70 }, { 56, 8, 69,  90 },
    };

    for (const auto& note : melody)
        pattern.appendChild (makeNote (1, note[0], note[1], note[2], note[3] / 100.0), nullptr);

    // A quiet counter-line an octave up, off the beat.
    for (int step = 2; step < 64; step += 8)
        pattern.appendChild (makeNote (4, step, 1, 88 - (step / 16) * 2, 0.35), nullptr);

    auto playlist = project.getChildWithName (ids::PLAYLIST);
    playlist.getChild (0).appendChild (makeClip (1, 0, 4), nullptr);

    return canonicalTree (project, projectSpec());
}

juce::ValueTree ProjectFactory::createEffectsDemo()
{
    auto project = createDefault();
    project.setProperty (ids::name, "Effect Chain", nullptr);
    project.setProperty (ids::tempoBpm, 110.0, nullptr);
    project.setProperty (ids::barsInSong, 4, nullptr);

    int index = 0;

    for (auto channel : project)
    {
        if (! channel.hasType (ids::CHANNEL))
            continue;

        auto instrument = channel.getChildWithName (ids::INSTRUMENT);
        auto amp = instrument.getChildWithName (ids::AMP);

        if (index == 0)
        {
            // A pad, through a filter and a chorus on the channel itself.
            channel.setProperty (ids::name, "Pad", nullptr);
            channel.setProperty (ids::volume, 0.30, nullptr);
            ProjectEdits::oscillatorAt (channel, 0).setProperty (ids::wave, "saw", nullptr);
            amp.setProperty (ids::attack, 0.35, nullptr);
            amp.setProperty (ids::sustain, 0.85, nullptr);
            amp.setProperty (ids::release, 0.9, nullptr);

            channel.appendChild (makeEffect (1, "filter", { { ids::cutoff, 900.0 },
                                                            { ids::resonance, 1.4 } }), nullptr);
            channel.appendChild (makeEffect (2, "chorus", { { ids::rate, 0.6 },
                                                            { ids::depth, 0.45 },
                                                            { ids::mix, 0.6 } }), nullptr);
        }
        else if (index == 1)
        {
            // A pluck feeding the delay on its mixer track.
            channel.setProperty (ids::name, "Pluck", nullptr);
            channel.setProperty (ids::volume, 0.35, nullptr);
            ProjectEdits::oscillatorAt (channel, 0).setProperty (ids::wave, "triangle", nullptr);
            amp.setProperty (ids::attack, 0.001, nullptr);
            amp.setProperty (ids::decay, 0.22, nullptr);
            amp.setProperty (ids::sustain, 0.0, nullptr);
            amp.setProperty (ids::release, 0.10, nullptr);
        }
        else if (index == 2)
        {
            channel.setProperty (ids::name, "Bass", nullptr);
            channel.setProperty (ids::volume, 0.45, nullptr);
            channel.appendChild (makeEffect (3, "drive", { { ids::drive, 6.0 },
                                                           { ids::outputGain, 0.9 } }), nullptr);
        }
        else
        {
            channel.setProperty (ids::name, "Kick", nullptr);
            channel.setProperty (ids::basePitch, 36, nullptr);
            channel.setProperty (ids::volume, 0.60, nullptr);
            ProjectEdits::oscillatorAt (channel, 0).setProperty (ids::wave, "sine", nullptr);
            amp.setProperty (ids::attack, 0.001, nullptr);
            amp.setProperty (ids::decay, 0.16, nullptr);
            amp.setProperty (ids::sustain, 0.0, nullptr);
        }

        ++index;
    }

    auto mixer = project.getChildWithName (ids::MIXER);
    int trackIndex = 0;

    for (auto track : mixer)
    {
        if (! track.hasType (ids::MIXER_TRACK))
            continue;

        // Four channels through reverb, delay and drive stack up fast; the
        // first version of this demo peaked at 1.77 and clipped every bar.
        track.setProperty (ids::gain, 0.55, nullptr);

        // Insert 2 carries the delay the pluck feeds; insert 1 gets the reverb
        // the pad sits in, so both places a chain can live are demonstrated.
        if (trackIndex == 0)
            track.appendChild (makeEffect (4, "reverb", { { ids::roomSize, 0.85 },
                                                          { ids::damping, 0.25 },
                                                          { ids::mix, 0.4 } }), nullptr);

        if (trackIndex == 1)
            track.appendChild (makeEffect (5, "delay", { { ids::delayMs, 340.0 },
                                                         { ids::feedback, 0.45 },
                                                         { ids::mix, 0.35 } }), nullptr);

        if (trackIndex == 2)
            track.appendChild (makeEffect (6, "eq", { { ids::lowGainDb, 5.0 },
                                                      { ids::midGainDb, -4.0 },
                                                      { ids::midFreq, 700.0 } }), nullptr);

        ++trackIndex;
    }

    auto pattern = project.getChildWithName (ids::PATTERN);
    pattern.setProperty (ids::name, "Wash", nullptr);
    pattern.setProperty (ids::lengthSteps, 32, nullptr);

    // Long pad chords, so the reverb and chorus have something to work on.
    for (int bar = 0; bar < 2; ++bar)
        for (const auto pitch : { 57, 64, 69 })
            pattern.appendChild (makeNote (1, bar * 16, 15, pitch + bar * 2, 0.6), nullptr);

    // Plucks on the offbeats, which is where a delay is audible as a delay.
    for (int step = 2; step < 32; step += 6)
        pattern.appendChild (makeNote (2, step, 1, 72 + (step % 12), 0.7), nullptr);

    for (int step = 0; step < 32; step += 8)
        pattern.appendChild (makeNote (3, step, 3, 40, 0.8), nullptr);

    for (int step = 0; step < 32; step += 4)
        pattern.appendChild (makeNote (4, step, 1, 36, 1.0), nullptr);

    auto playlist = project.getChildWithName (ids::PLAYLIST);
    playlist.getChild (0).appendChild (makeClip (1, 0, 4), nullptr);

    return canonicalTree (project, projectSpec());
}

juce::ValueTree ProjectFactory::createAutomationDemo()
{
    auto project = createDefault();
    project.setProperty (ids::name, "Automation", nullptr);
    project.setProperty (ids::tempoBpm, 126.0, nullptr);
    project.setProperty (ids::barsInSong, 8, nullptr);

    int index = 0;

    for (auto channel : project)
    {
        if (! channel.hasType (ids::CHANNEL))
            continue;

        auto instrument = channel.getChildWithName (ids::INSTRUMENT);
        auto amp = instrument.getChildWithName (ids::AMP);

        if (index == 0)
        {
            channel.setProperty (ids::name, "Arp", nullptr);
            channel.setProperty (ids::volume, 0.6, nullptr);
            ProjectEdits::oscillatorAt (channel, 0).setProperty (ids::wave, "saw", nullptr);
            amp.setProperty (ids::attack, 0.002, nullptr);
            amp.setProperty (ids::decay, 0.16, nullptr);
            amp.setProperty (ids::sustain, 0.25, nullptr);
            amp.setProperty (ids::release, 0.12, nullptr);

            // The filter this demo sweeps. Wide open to start with, so the
            // sweep has somewhere to travel from.
            channel.appendChild (makeEffect (1, "filter", { { ids::cutoff, 16000.0 },
                                                            { ids::resonance, 2.2 } }), nullptr);
        }
        else if (index == 1)
        {
            channel.setProperty (ids::name, "Kick", nullptr);
            channel.setProperty (ids::basePitch, 36, nullptr);
            channel.setProperty (ids::volume, 0.95, nullptr);
            ProjectEdits::oscillatorAt (channel, 0).setProperty (ids::wave, "sine", nullptr);
            amp.setProperty (ids::attack, 0.001, nullptr);
            amp.setProperty (ids::decay, 0.15, nullptr);
            amp.setProperty (ids::sustain, 0.0, nullptr);
        }
        else
        {
            channel.setProperty (ids::muted, true, nullptr);
        }

        ++index;
    }

    auto pattern = project.getChildWithName (ids::PATTERN);
    pattern.setProperty (ids::name, "Arp", nullptr);
    pattern.setProperty (ids::lengthSteps, 16, nullptr);

    const int arp[] = { 57, 60, 64, 69, 64, 60, 64, 69,
                        57, 60, 64, 72, 69, 64, 60, 64 };

    for (int step = 0; step < 16; ++step)
        pattern.appendChild (makeNote (1, step, 1, arp[step], step % 4 == 0 ? 0.9 : 0.6), nullptr);

    for (int step = 0; step < 16; step += 4)
        pattern.appendChild (makeNote (2, step, 1, 36, 1.0), nullptr);

    // Two curves: the filter opening across four bars, and a fade at the end.
    // 16 steps to a bar, so a four-bar clip is 64 steps of curve.
    project.appendChild (makeAutomation (1, "Arp > Filter > Cutoff", "channelEffect", 1, 0,
                                         ids::cutoff,
                                         { { 0.0, 0.15 }, { 32.0, 0.55 }, { 64.0, 1.0 } }), nullptr);

    project.appendChild (makeAutomation (2, "Master > Gain", "master", 0, -1, ids::gain,
                                         { { 0.0, 0.9 }, { 48.0, 0.9 }, { 64.0, 0.0 } }), nullptr);

    auto playlist = project.getChildWithName (ids::PLAYLIST);
    playlist.getChild (0).appendChild (makeClip (1, 0, 8), nullptr);

    // The sweep over the first four bars, the fade over the last four.
    playlist.getChild (1).appendChild (makeAutomationClip (1, 0, 4), nullptr);
    playlist.getChild (2).appendChild (makeAutomationClip (2, 4, 4), nullptr);

    return canonicalTree (project, projectSpec());
}

const std::vector<ProjectFactory::Demo>& ProjectFactory::demos()
{
    static const std::vector<Demo> library {
        { "demo.dew",       "Getting Started",
          "A four-bar groove: kick, snare, bass and a lead line.", &ProjectFactory::createDemo },
        { "melody.dew",     "Piano Roll",
          "Chords, held notes and a velocity shape.", &ProjectFactory::createMelodyDemo },
        { "effects.dew",    "Effect Chain",
          "A pad through a filter and chorus, a pluck into a delay, reverb on the mix.",
          &ProjectFactory::createEffectsDemo },
        { "automation.dew", "Automation",
          "A filter sweep over four bars, then a fade.", &ProjectFactory::createAutomationDemo },
    };

    return library;
}

} // namespace dew
