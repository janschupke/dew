#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace dew
{

class AudioEngine;
class RenderJob;
class SamplePool;
class SoundFontPool;

namespace control
{

/** Everything an operation needs that lives above this library.

    dew_control links dew_io and stops there, so it can see the document's
    SHAPE - ProjectEdits, the schema, the automation tables - and none of the
    things that own an open one: ProjectDocument is dew_app's and MainComponent
    is dew_ui's, and both are above. This interface is how they arrive, and it
    is the pattern SampleProvider.h and SoundFontProvider.h already established
    for the same problem one layer down.

    The point is not decoupling for its own sake. It is that every operation in
    this library can then be driven by a test that owns nothing but a ValueTree
    and an UndoManager - no window, no message loop, no audio device, no socket -
    which is what makes a table of thirty operations testable at all.

    **Message thread only.** Every method here touches the document or the
    interface. The server calls them through MessageThreadCall and never from
    the socket thread.

    Everything a headless caller cannot answer has a default that refuses,
    rather than a pure virtual a test would have to stub. A refusal is a real
    answer - "this build cannot save" - and it keeps the fake in the tests to
    the two methods that matter.
*/
class ControlHost
{
public:
    virtual ~ControlHost() = default;

    /** The PROJECT node. Invalid if nothing is open. */
    virtual juce::ValueTree project() = 0;

    /** The undo manager every mutation must be given.

        Not optional and not nullable by contract: ProjectEdits takes an
        UndoManager* on every call precisely so that nothing bypasses undo, and
        an operation handing it nullptr would be a change the user cannot take
        back - which is the one thing the consent dialog promises they can.
    */
    virtual juce::UndoManager* undoManager() = 0;

    /** Applies any pending engine rebuild NOW rather than at the next turn of
        the message loop.

        A document write reaches the engine through an AsyncUpdater, which
        coalesces - the reason a knob drag costs one rebuild rather than one per
        frame. That is right for a drag and wrong for a request that writes and
        then answers: without this, a caller that sets a tempo and immediately
        asks what is playing is told the old one, and the disagreement lasts
        exactly as long as nobody is turning the message loop.
    */
    virtual void flushEngine() {}

    /** The transport, or nullptr where there is none. */
    virtual AudioEngine* engine()
    {
        return nullptr;
    }

    /** The one render thread, or nullptr where there is none.

        A render is seconds to minutes of work and cannot be done on the message
        thread - LAME runs its encoder from the writer's DESTRUCTOR and blocks
        until the child process exits - so an operation starts the job and
        answers immediately.
    */
    virtual RenderJob* renderJob()
    {
        return nullptr;
    }

    /** Where audio and soundfont channels get their material.

        Both are load-bearing, and forgetting either is silent: OfflineRenderer
        builds its OWN snapshot, so a render given no sample pool writes the
        synth parts alone and says nothing about the recordings it dropped.
    */
    virtual SamplePool* samplePool()
    {
        return nullptr;
    }

    virtual SoundFontPool* soundFontPool()
    {
        return nullptr;
    }

    // --- the document as a file ----------------------------------------------
    /** Replaces the open project with an empty one. False if the host refuses -
        which it should when there are unsaved changes it cannot ask about. */
    virtual bool newProject()
    {
        return false;
    }

    virtual bool openProject (const juce::File&)
    {
        return false;
    }

    /** Saves to the file the project already has. False when it has none: a
        caller that wants a path says so with saveProjectAs. */
    virtual bool saveProject()
    {
        return false;
    }

    virtual bool saveProjectAs (const juce::File&)
    {
        return false;
    }

    virtual juce::File projectFile() const
    {
        return {};
    }

    virtual bool isProjectModified() const
    {
        return false;
    }
};

} // namespace control
} // namespace dew
