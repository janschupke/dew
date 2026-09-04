#pragma once

#include "control/ControlOps.h"
#include "control/ControlValue.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"

namespace dew::control
{

/** A juce::var object, built a member at a time.

    juce::var has no object literal, so every answer in this library would
    otherwise be four lines of DynamicObject bookkeeping repeated thirty times.
    Chainable, and convertible to var, so a result reads as the shape it is.
*/
struct Obj
{
    Obj()
        : object (new juce::DynamicObject())
    {
    }

    /** An Identifier rather than a literal, so a field named after a document
        property is spelled once - `set (ids::muted, ...)` rather than a
        `"muted"` beside it that a rename would leave behind. */
    Obj& set (const juce::Identifier& key, const juce::var& value)
    {
        object->setProperty (key, value);
        return *this;
    }

    operator juce::var () const // NOLINT(google-explicit-constructor)
    {
        return juce::var (object.get());
    }

    juce::DynamicObject::Ptr object;
};

/** An array of vars, built an element at a time. */
inline juce::var arrayOf (const juce::Array<juce::var>& items)
{
    return juce::var (items);
}

/** What every batch write answers with.

    A count rather than nothing, because the failure a bare success hides is a
    filter that matched no rows: a caller told only "ok" cannot tell "I removed
    the four clips you meant" from "I removed nothing and you misspelled a
    track name". `skipped` carries the rest of that answer.
*/
inline ControlResult applied (int count, int skipped = 0)
{
    return ControlResult::success (Obj {}.set ("applied", count).set ("skipped", skipped));
}

/** Opens the ONE undo transaction a call is allowed.

    Every operation in this table is one undo step, however many entries its
    batch carried, because that is the promise the consent dialog makes: the
    user's protection against a client that does something surprising is Cmd-Z,
    and a batch that arrives as two hundred steps is not undoable in any sense a
    person would recognise.

    Named for the operation, in the same shape the interface's own transactions
    use. Undo transaction names are deliberately not translated - nothing but
    getUndoDescription reads one, and dew's Edit menu shows the command's name.
*/
inline void beginOneTransaction (ControlHost& host, const char* opName)
{
    if (auto* undo = host.undoManager())
        undo->beginNewTransaction (juce::String ("Agent: ") + opName);
}

// --- lookups that report ------------------------------------------------------
// ProjectEdits' finders answer with an invalid tree, which is right for a view
// that simply draws nothing. A protocol has to SAY which id was not found, so
// these pair the lookup with the sentence.

inline juce::String noSuchChannel (int id)
{
    return "no channel with id " + juce::String (id) + ".";
}

inline juce::String noSuchPattern (int id)
{
    return "no pattern with id " + juce::String (id) + ".";
}

inline juce::String noSuchMixerTrack (int id)
{
    return "no mixer track with id " + juce::String (id) + ".";
}

inline juce::String noSuchTrack (int index)
{
    return "no playlist track at index " + juce::String (index) + ".";
}

/** The nth playlist track, or an invalid tree.

    Playlist tracks carry no id - they are positional, unlike channels and
    mixer inserts - so an index is the only address there is, and it is the
    address the interface uses too.
*/
inline juce::ValueTree playlistTrackAt (const juce::ValueTree& project, int index)
{
    const auto playlist = project.getChildWithName (ids::PLAYLIST);

    if (index < 0 || index >= playlist.getNumChildren())
        return {};

    const auto track = playlist.getChild (index);

    return track.hasType (ids::PLAYLIST_TRACK) ? track : juce::ValueTree {};
}

/** Every PLAYLIST_TRACK, in order. */
inline juce::Array<juce::ValueTree> playlistTracks (const juce::ValueTree& project)
{
    juce::Array<juce::ValueTree> tracks;

    for (const auto& track : project.getChildWithName (ids::PLAYLIST))
        if (track.hasType (ids::PLAYLIST_TRACK))
            tracks.add (track);

    return tracks;
}

/** The node an owner address names - a channel, a mixer insert, or the master.

    An effect chain hangs off exactly three kinds of node and every operation
    that touches one addresses it the same way, so the resolution is here rather
    than in each of them. It is deliberately the same `target` vocabulary
    ParamAddress uses, so a caller that can address a parameter can address a
    chain without learning a second spelling.
*/
inline juce::ValueTree chainOwnerFor (const juce::ValueTree& project, const juce::String& target,
                                      int id)
{
    if (target == "channel")
        return ProjectEdits::findChannel (project, id);

    if (target == "mixerTrack")
        return ProjectEdits::findMixerTrack (project, id);

    if (target == "master")
        return project.getChildWithName (ids::MIXER).getChildWithName (ids::MASTER);

    return {};
}

/** The children of a project of one type, in document order. */
inline juce::Array<juce::ValueTree> childrenOfType (const juce::ValueTree& parent,
                                                    const juce::Identifier& type)
{
    juce::Array<juce::ValueTree> found;

    for (const auto& child : parent)
        if (child.hasType (type))
            found.add (child);

    return found;
}

} // namespace dew::control
