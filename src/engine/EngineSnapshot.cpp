#include "engine/EngineSnapshot.h"

#include "engine/SnapshotReaders.h"

#include "engine/SampleProvider.h"

#include <atomic>
#include <cmath>

#include "model/GeneratorCatalog.h"
#include "model/Ids.h"
#include "model/InstrumentType.h"
#include "model/Meter.h"

namespace dew
{

int EngineSnapshot::patternIndexForId (int patternId) const
{
    for (size_t i = 0; i < patterns.size(); ++i)
        if (patterns[i].id == patternId)
            return (int) i;

    return -1;
}

int EngineSnapshot::channelIndexForId (int channelId) const
{
    for (size_t i = 0; i < channels.size(); ++i)
        if (channels[i].id == channelId)
            return (int) i;

    return -1;
}

const std::shared_ptr<const TempoMap>& defaultTempoMap()
{
    static const std::shared_ptr<const TempoMap> map = std::make_shared<const TempoMap> (
        TempoMap::constant (128.0, 4));

    return map;
}

int EngineSnapshot::songLengthSteps() const
{
    int end = 0;

    // Every kind of clip counts. A sweep placed after the last note is still
    // part of the arrangement, and leaving automation out made the song loop
    // out from underneath it; an arrangement of nothing but recordings has the
    // same problem in a starker form - it would have no length at all.
    for (const auto& clip : clips)
        if (clip.patternIndex >= 0 || clip.automationIndex >= 0 || clip.channelIndex >= 0)
            end = juce::jmax (end, (clip.startBar + clip.lengthBars) * stepsPerBar());

    return end;
}

bool EngineSnapshot::isChannelAudible (const ChannelSnapshot& channel) const noexcept
{
    return isChannelAudible (channel, channel.muted);
}

bool EngineSnapshot::isChannelAudible (const ChannelSnapshot& channel,
                                       bool mutedOverride) const noexcept
{
    // One state per channel: muted or audible. There used to be a solo beside
    // it and a snapshot-wide anyChannelSolo composing the two, which is what
    // made a channel's audibility a fact about every OTHER channel as well.
    juce::ignoreUnused (channel);

    return ! mutedOverride;
}

bool EngineSnapshot::isSilent() const
{
    if (channels.empty())
        return true;

    for (const auto& pattern : patterns)
        for (const auto& note : pattern.notes)
            if (note.channelIndex >= 0)
                return false;

    // An audio-only project has no notes at all. Without this it reports
    // "nothing to play", and dew_render exits non-zero on a perfectly good
    // arrangement of recordings.
    for (const auto& clip : clips)
        if (clip.channelIndex >= 0 && channels[(size_t) clip.channelIndex].audio != nullptr)
            return false;

    return true;
}

namespace
{

std::atomic<juce::uint64> nextGeneration { 1 };

} // namespace

float AutomationSnapshot::valueAt (double step) const noexcept
{
    // curveValueAt, not a copy of it. This function and
    // ProjectEdits::automationValueAt were the same arithmetic written twice in
    // two layers; the editor drew one of them and the audio thread played the
    // other, and nothing made them agree.
    const auto normalised = curveValueAt (points, step);

    // Same mapping the picker and the point editor use, so what is drawn is
    // what is heard - and the same SNAP, so a curve over a toggle or a filter
    // mode never lands between two states.
    if (spec == nullptr)
        return 0.0f;

    return (float) automationValueFor (*spec, normalised);
}

EngineSnapshot buildSnapshot (const juce::ValueTree& project, juce::StringArray* warnings,
                              SampleProvider* samples, SoundFontProvider* soundFonts)
{
    const auto warn = [warnings] (const juce::String& message)
    {
        if (warnings != nullptr)
            warnings->add (message);
    };

    EngineSnapshot snapshot;
    snapshot.generation = nextGeneration.fetch_add (1, std::memory_order_relaxed);

    // Never null, including on the early return below: the field defaults to a
    // shared constant map, so everything that converts a step into time can read
    // it without a null check on the render path.
    if (! project.isValid())
        return snapshot;

    snapshot.tempoBpm = juce::jlimit (20.0, 999.0, (double) project[ids::tempoBpm]);

    // Through Meter rather than read here, so the engine's idea of a bar and
    // the editors' cannot drift apart - both clamp the same way and both treat
    // an absent property as 4/4.
    const auto meter = Meter::of (project);
    snapshot.stepsPerBeat = meter.stepsPerBeat;
    snapshot.beatsPerBar = meter.beatsPerBar;
    snapshot.beatUnit = meter.beatUnit;

    // Which pool unit each effect id has claimed, for the whole project. -1 is
    // free; the map is rebuilt from scratch every time, and is a pure function
    // of the ids present, so it comes out identical for an unchanged document.
    std::array<int, kMaxEffectUnits> unitOwners;
    unitOwners.fill (-1);

    // --- mixer ---------------------------------------------------------------
    const auto mixer = project.getChildWithName (ids::MIXER);
    const auto master = mixer.getChildWithName (ids::MASTER);
    snapshot.masterGain = requireMixerTrackParamSpec (ids::gain).clamp (
        (float) (double) master[ids::gain]);

    // Before the tracks, so the master's effects claim their pool units first
    // and adding an insert cannot move them.
    snapshot.masterEffects = snapshotRead::readEffectChain (master, "Master", unitOwners, warn);

    for (const auto& track : mixer)
    {
        if (! track.hasType (ids::MIXER_TRACK))
            continue;

        if ((int) snapshot.mixerTracks.size() >= kMaxMixerTracks)
        {
            warn ("More than " + juce::String (kMaxMixerTracks)
                  + " mixer tracks; the rest are not rendered.");
            break;
        }

        MixerTrackSnapshot m;
        m.id = (int) track[ids::id];
        m.gain = requireMixerTrackParamSpec (ids::gain).clamp ((float) (double) track[ids::gain]);
        m.pan = requireMixerTrackParamSpec (ids::pan).clamp ((float) (double) track[ids::pan]);
        m.mute = (bool) track[ids::mute];

        m.effects = snapshotRead::readEffectChain (
            track, "Mixer track " + track[ids::name].toString(), unitOwners, warn);

        snapshot.mixerTracks.push_back (m);
    }

    // --- channels ------------------------------------------------------------
    for (const auto& channel : project)
    {
        if (! channel.hasType (ids::CHANNEL))
            continue;

        if ((int) snapshot.channels.size() >= kMaxChannels)
        {
            warn ("More than " + juce::String (kMaxChannels)
                  + " channels; the rest are not rendered.");
            break;
        }

        ChannelSnapshot c;
        c.id = (int) channel[ids::id];
        c.volume = juce::jlimit (0.0f, 1.0f, (float) (double) channel[ids::volume]);
        c.pan = juce::jlimit (-1.0f, 1.0f, (float) (double) channel[ids::pan]);
        c.muted = (bool) channel[ids::muted];

        const auto instrument = channel.getChildWithName (ids::INSTRUMENT);
        c.osc = snapshotRead::readOscBank (
            instrument, "Channel \"" + channel[ids::name].toString() + "\"", warn);
        c.amp = snapshotRead::readAmp (instrument.getChildWithName (ids::AMP));

        // Resolve the mixer routing now; the audio thread must not search.
        const int mixerTrackId = (int) channel[ids::mixerTrackId];
        c.mixerTrackIndex = -1;

        for (size_t i = 0; i < snapshot.mixerTracks.size(); ++i)
            if (snapshot.mixerTracks[i].id == mixerTrackId)
                c.mixerTrackIndex = (int) i;

        if (c.mixerTrackIndex < 0 && ! snapshot.mixerTracks.empty())
        {
            warn ("Channel \"" + channel[ids::name].toString() + "\" routes to mixer track "
                  + juce::String (mixerTrackId)
                  + ", which does not exist; using the first insert.");
            c.mixerTrackIndex = 0;
        }

        c.effects = snapshotRead::readEffectChain (
            channel, "Channel \"" + channel[ids::name].toString() + "\"", unitOwners, warn);

        // Through the catalog, and REPORTED when it is not one dew knows. This
        // was a ternary against "audio", so every other string - a kind added
        // by a newer build, a typo in a hand-edited file - became a synth with
        // nothing said, and the project simply played back wrong. It is the
        // defect effectTypeFor returning an optional was written to close, in
        // the half of the app that had not had it done yet.
        const auto sourceId = channel[ids::source].toString();
        const auto source = instrumentTypeFor (sourceId);

        if (! source.has_value())
            warn ("Channel \"" + channel[ids::name].toString() + "\" plays \"" + sourceId
                  + "\", which this build does not know; playing it as a synth.");

        c.source = source.value_or (InstrumentType::synth);

        if (c.source == InstrumentType::audio)
            snapshotRead::readSample (c, channel, samples, warn);
        else if (c.source == InstrumentType::soundfont)
            snapshotRead::readSoundFont (c, channel, soundFonts, warn);

        snapshot.channels.push_back (c);
    }

    // Lets the engine skip the whole stereo effect stage on a project with none.
    for (const auto& channel : snapshot.channels)
        snapshot.anyEffects = snapshot.anyEffects || channel.effects.anyEnabled();

    for (const auto& track : snapshot.mixerTracks)
        snapshot.anyEffects = snapshot.anyEffects || track.effects.anyEnabled();

    snapshot.anyEffects = snapshot.anyEffects || snapshot.masterEffects.anyEnabled();

    // --- patterns ------------------------------------------------------------
    for (const auto& pattern : project)
    {
        if (! pattern.hasType (ids::PATTERN))
            continue;

        PatternSnapshot p;
        p.id = (int) pattern[ids::id];
        p.lengthSteps = juce::jmax (1, (int) pattern[ids::lengthSteps]);

        for (const auto& note : pattern)
        {
            if (! note.hasType (ids::NOTE))
                continue;

            NoteSnapshot n;
            n.channelIndex = snapshot.channelIndexForId ((int) note[ids::ch]);
            n.step = juce::jmax (0, (int) note[ids::step]);
            n.lengthSteps = juce::jmax (1, (int) note[ids::lengthSteps]);
            n.pitch = juce::jlimit (0, 127, (int) note[ids::pitch]);
            n.velocity = juce::jlimit (0.0f, 1.0f, (float) (double) note[ids::velocity]);

            if (n.channelIndex < 0)
            {
                warn ("A note in pattern " + juce::String (p.id) + " refers to channel "
                      + note[ids::ch].toString() + ", which does not exist; it will not sound.");
                continue;
            }

            // A note starting past the end of its pattern would never play.
            if (n.step >= p.lengthSteps)
            {
                warn ("A note in pattern " + juce::String (p.id) + " starts at step "
                      + juce::String (n.step) + ", past the pattern's length; it will not sound.");
                continue;
            }

            p.notes.push_back (n);
        }

        snapshot.patterns.push_back (p);
    }

    // --- automations ---------------------------------------------------------
    // Resolved before clips, since a clip refers to one by index.
    juce::Array<int> automationIds;

    for (const auto& automation : project)
    {
        if (! automation.hasType (ids::AUTOMATION))
            continue;

        if ((int) snapshot.automations.size() >= kMaxAutomations)
        {
            warn ("More than " + juce::String (kMaxAutomations)
                  + " automations; the rest are ignored.");
            break;
        }

        AutomationSnapshot a;
        a.scope = automationScopeFromString (automation[ids::scope].toString());
        a.slotIndex = (int) automation[ids::slot];

        const juce::Identifier property (automation[ids::param].toString());
        a.param = snapshotRead::automationParamFromIdentifier (a.scope, property);

        const auto targetId = (int) automation[ids::targetId];
        juce::String effectType;

        // Resolve the owner to an index, and an effect target to its type, so
        // the audio thread never searches and never compares a string.
        const auto resolveChain = [&] (const juce::ValueTree& owner) -> bool
        {
            int slot = 0;

            for (const auto& effect : owner)
            {
                if (! effect.hasType (ids::EFFECT))
                    continue;

                if (slot++ == a.slotIndex)
                {
                    effectType = effect[ids::type].toString();
                    return true;
                }
            }

            return false;
        };

        bool resolved = false;

        switch (a.scope)
        {
            case AutomationScope::project:
            case AutomationScope::master:
                // Neither names a node to find: the master is the one bus every
                // project has, and the project scope is the arrangement itself.
                a.targetIndex = -1;
                resolved = true;
                break;

            case AutomationScope::channel:
            case AutomationScope::channelOsc:
            case AutomationScope::channelAmp:
            case AutomationScope::channelSoundFont:
            case AutomationScope::channelEffect:
                for (size_t i = 0; i < snapshot.channels.size(); ++i)
                    if (snapshot.channels[i].id == targetId)
                        a.targetIndex = (int) i;

                resolved = a.targetIndex >= 0;

                // The slot has to exist, and the parameter has to be one the
                // generator it runs actually reads - otherwise the clip is
                // dropped rather than left pointing at something the voice will
                // never look at.
                //
                // Asked of the MODEL, which owns the split, rather than
                // re-derived here as "is this slot a wavetable". That spelling
                // dropped a curve over a CLASSIC slot's gain too, because it
                // asked about the slot where the question is about the
                // parameter.
                if (resolved && a.scope == AutomationScope::channelOsc)
                {
                    const auto& slots = snapshot.channels[(size_t) a.targetIndex].osc;

                    resolved = a.slotIndex >= 0 && a.slotIndex < slots.numSlots
                               && ! isForeignGeneratorParam (
                                   oscModeToString (slots.slots[(size_t) a.slotIndex].mode),
                                   property);
                }

                // Both are gated on the channel actually being that kind of
                // instrument. Every channel carries an AMP and a SOUNDFONT node
                // inert to keep the canonical tree one shape, so without this a
                // curve saved against a channel that has since been switched to
                // another source would be applied to settings nothing reads -
                // the same rule the oscillator check above states for a slot.
                if (resolved && a.scope == AutomationScope::channelAmp)
                    resolved = snapshot.channels[(size_t) a.targetIndex].source
                               == InstrumentType::synth;

                if (resolved && a.scope == AutomationScope::channelSoundFont)
                    resolved = snapshot.channels[(size_t) a.targetIndex].source
                               == InstrumentType::soundfont;

                if (resolved && a.scope == AutomationScope::channelEffect)
                {
                    resolved = false;

                    for (const auto& channel : project)
                        if (channel.hasType (ids::CHANNEL) && (int) channel[ids::id] == targetId)
                            resolved = resolveChain (channel);
                }
                break;

            case AutomationScope::mixerTrack:
            case AutomationScope::mixerEffect:
                for (size_t i = 0; i < snapshot.mixerTracks.size(); ++i)
                    if (snapshot.mixerTracks[i].id == targetId)
                        a.targetIndex = (int) i;

                resolved = a.targetIndex >= 0;

                if (resolved && a.scope == AutomationScope::mixerEffect)
                {
                    resolved = false;

                    for (const auto& track : mixer)
                        if (track.hasType (ids::MIXER_TRACK) && (int) track[ids::id] == targetId)
                            resolved = resolveChain (track);
                }
                break;
        }

        // A target that no longer exists - a deleted channel, or an effect slot
        // that changed type so the parameter no longer applies - is dropped
        // with a warning, exactly like a clip pointing at a missing pattern.
        const auto* spec = resolved ? findParamSpec (a.scope, effectType, property) : nullptr;

        if (spec == nullptr || a.param == AutomationParam::none)
        {
            warn ("Automation \"" + automation[ids::name].toString()
                  + "\" targets something that no longer exists; it is ignored.");
            automationIds.add (-1);
            snapshot.automations.push_back ({});
            continue;
        }

        a.spec = spec;

        if (a.scope == AutomationScope::channelEffect || a.scope == AutomationScope::mixerEffect)
            if (const auto type = effectTypeFor (effectType))
                a.paramIndex = effectParamIndex (*type, property);

        // Read by the same function the editor reads them with, so a property
        // added to a point cannot reach one of the two and not the other.
        a.points = curvePointsOf (automation);

        // The tree is kept sorted by ProjectEdits, but a hand-edited file is
        // not, and curveValueAt requires sorted input rather than sorting a
        // third time on every block.
        std::stable_sort (a.points.begin(), a.points.end(),
                          [] (const auto& x, const auto& y) { return x.step < y.step; });

        automationIds.add ((int) automation[ids::id]);
        snapshot.automations.push_back (std::move (a));
    }

    // --- playlist ------------------------------------------------------------
    // There was a pre-scan here, because solo had to be known before any clip
    // was resolved: whether a lane was audible depended on every other lane.
    // One state per track means a lane answers for itself.
    for (const auto& track : project.getChildWithName (ids::PLAYLIST))
    {
        if (! track.hasType (ids::PLAYLIST_TRACK))
            continue;

        const auto trackAudible = ! (bool) track[ids::mute];

        // Clamped rather than trusted: a hand-edited file can say anything, and
        // a negative gain would invert every note the lane triggers.
        const auto trackGain = juce::jlimit (0.0f, 1.0f, (float) (double) track[ids::gain]);

        for (const auto& clip : track)
        {
            if (! clip.hasType (ids::CLIP))
                continue;

            ClipSnapshot c;
            c.startBar = juce::jmax (0, (int) clip[ids::startBar]);
            c.lengthBars = juce::jmax (1, (int) clip[ids::lengthBars]);
            c.trackAudible = trackAudible;
            c.trackGain = trackGain;

            if (clip[ids::kind].toString() == "automation")
            {
                c.automationIndex = automationIds.indexOf ((int) clip[ids::automationId]);

                if (c.automationIndex < 0)
                {
                    warn ("A clip refers to automation " + clip[ids::automationId].toString()
                          + ", which does not exist; it will do nothing.");
                    continue;
                }

                // A muted track silences its notes; it should silence what its
                // automation does too, or a muted lane still moves the mix.
                if (trackAudible)
                    snapshot.anyAutomation = true;

                snapshot.clips.push_back (c);
                continue;
            }

            if (clip[ids::kind].toString() == "audio")
            {
                const auto channelId = (int) clip[ids::channelId];
                c.channelIndex = snapshot.channelIndexForId (channelId);

                if (c.channelIndex < 0)
                {
                    warn ("A clip refers to channel " + juce::String (channelId)
                          + ", which does not exist; it will not play.");
                    continue;
                }

                snapshot.clips.push_back (c);
                continue;
            }

            c.patternIndex = snapshot.patternIndexForId ((int) clip[ids::patternId]);

            if (c.patternIndex < 0)
            {
                warn ("A clip refers to pattern " + clip[ids::patternId].toString()
                      + ", which does not exist; it will not play.");
                continue;
            }

            snapshot.clips.push_back (c);
        }
    }

    // LAST, because it reads the automations and the clips that were just
    // resolved. Without a tempo curve it is the constant form, which is exactly
    // the arithmetic every render has always done.
    snapshot.tempoMap = std::make_shared<const TempoMap> (TempoMap::build (snapshot, warnings));

    return snapshot;
}

} // namespace dew
