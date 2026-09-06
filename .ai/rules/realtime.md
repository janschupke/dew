# The audio thread

Two gate suites hold this: `tests/AllocationGateTests.cpp` ("the render path allocates
nothing", "the sequencer refuses at its trigger bound rather than growing") and
`tests/RealtimeTests.cpp` ("what the render path copies owns nothing", "the render path
takes no snapshot entry by value"). Both are in the ordinary suite; you will not get past
the gate with an allocation in `process`.

- **The render path allocates nothing.** No `std::vector` growth, no `juce::String`, no
  `new`. Size everything in `prepare()`. A snapshot entry is read by reference or by a
  trivially copyable value — never by a value that owns.
- **The sequencer refuses at its trigger bound rather than growing.** Dropping a trigger is
  the designed behaviour; reallocating is not.
- **`dev` bounds-checks `operator[]`** (`_LIBCPP_HARDENING_MODE_DEBUG`) and the render path
  indexes a snapshot's channels every block. Use `dev` while working on it.

## Effect modules are never destroyed

Effect *parameters* travel in the snapshot like everything else. Effect *instances* own
state that must survive a snapshot swap, and dew deliberately has **no lock-free command
queue** — do not add one.

- Modules are built on the message thread on first use and **never destroyed while the
  engine lives**. `SnapshotBridge` has no acknowledgement path, so the message thread
  cannot learn when the audio thread has finished with an old snapshot: anything a
  snapshot points at must outlive every snapshot. Freeing an "unused" module hands the
  audio thread a dangling pointer that no test catches reliably.
- Pool slots key on each effect's **persistent id** by open addressing, not on position.
  Positional keying renumbers later slots when an earlier chain changes and cuts the
  reverb tail they were in the middle of. The assignment is `claimEffectUnit` in
  `src/engine/SnapshotReaders.cpp` — on the MESSAGE thread, while the snapshot is being
  built, which is the point: the pool itself (`src/engine/EffectModulePool.*`) holds the
  units and is handed an index. `EffectTests.cpp` guards this; verify a change to it by
  making the assignment positional and confirming the test fails.
- Switching a slot's type is `reset()`, not a reallocation — the pool holds all effect
  types at max size, so topology travels in the snapshot like everything else.

## Three queues, three different contracts

`PreviewQueue` (single-producer ring), `SnapshotBridge` (triple-buffered publish through
one compare-and-swap) and `SignalTap` (overwrite-without-asking, `std::atomic<float>`
slots, one monotonic count) each answer a different question, and none of the three is a
default choice. Read [Three queues, three different
contracts](#three-queues-three-different-contracts), below, before touching any of them or
adding a fourth.

`ctest --preset tsan` exists for exactly these three, and is **CI-only** — the TSan runtime
does not work on this machine. A race here looks like a flake rather than a failure.

The engine knows nothing about audio devices: it is prepared with a sample rate and a
block size and fills a buffer, which is why live playback and offline rendering run the
same code and a passing render says something about the real engine. Keep it that way.

## The click is summed after the meter

`AudioEngine::renderMetronome` runs **after** `buffer.applyGain (masterGain)`, after
`recordPeak (masterPeak, …)` and after `signalTap.write (…)`, and **before**
`transport.advance` — the last so it reads the same position the sequencer did.

That placement is the rule: a click is a monitoring aid, not part of the mix. It must not
colour the master meter, must not appear in the oscilloscope, and must not be ridden by
anything on the master chain. It can never reach a file either, because `OfflineRenderer`
builds its own `AudioEngine` and the flag defaults to off — "a render carries no clicks" in
`tests/MetronomeTests.cpp` pins that, because the way to lose it (a metronome option
threaded into `RenderOptions`) would look like a feature.

`Metronome` is a voice and nothing more: it is struck and it renders. Which beats fall
inside a block is `AudioEngine`'s, scanned the way `Sequencer::collect` scans note starts
and **through the tempo map**, not through `samplesPerStep()` — a scalar rate drifts against
a ramp, and the drift accumulates against the sample counter. A block is rendered in
segments around the strikes, so several clicks in one buffer need no queue and have no drop
policy.

**A count-in is one line**: `isPlayingNow = playing && ! countingIn`. That already gates the
sequencer, the automation, `transport.advance` and — the one that is easy to miss —
`blockContext.transport.isPlaying`, which a frozen transport claiming to play would leave
`SamplePlayer` re-rendering the same window of the same sample every block. The budget is
spent in whole blocks, so the clicks are anchored to the END of it: the last one is exactly
a beat before the downbeat and only the lead-in absorbs the rounding. `AudioRecorder`'s
pre-roll counts down by the same `numSamples` in the same device callback, which is what
keeps the take and the click in step with no cross-object read.

## Why it is this way

### Effects without a command queue

Effect *parameters* travel in the snapshot like everything else. Effect *instances* own
state — delay lines, reverb tanks — that has to survive a snapshot swap, and the usual
answer is a lock-free command queue.

dew does not have one. Modules are built on the message thread on first use, keyed on
(slot, type), and **never destroyed while the engine lives**. That is a correctness
argument rather than thrift: `SnapshotBridge` has no acknowledgement path, so the message
thread cannot learn when the audio thread has finished with an old snapshot, and anything
a snapshot points at must outlive every snapshot. Freeing an "unused" module hands the
audio thread a dangling pointer that no test catches reliably.

Slots are keyed on each effect's persistent id by open addressing, so the mapping is a
pure function of the ids in the document: adding an effect to an earlier channel does not
renumber the later ones and cut the reverb tail they were in the middle of.

### Three queues, three different contracts

Clicking a piano key has to make a sound without the sequencer running, so preview notes
reach the audio thread through **`PreviewQueue`** — a single-producer ring, not a
latest-wins atomic, because both halves of a fast click can land inside one 5.8ms block
and latest-wins would let the release overwrite the press.

**`SnapshotBridge`** hands over whole states, because half a state is nonsense: a
triple-buffered publish through a single compare-and-swap.

**`SignalTap`** is the only thing that runs audio → message, and it wants the opposite of
both: a visualiser needs the newest two thousand samples and nothing older. So it is a
ring that overwrites without asking, plus one monotonic count. The writer never waits.
The reader works out which *absolute* sample indices it is copying, copies them, and asks
the count again — if the writer has moved on by more than the ring holds, the display
keeps the frame it had. The slots are `std::atomic<float>` rather than a plain array, and
that is the safety argument rather than a decoration: copy-then-check over a plain array
is a data race, which is undefined behaviour rather than a merely stale value.

The engine knows nothing about audio devices. It is prepared with a sample rate and a
block size and fills a buffer, so live playback and offline rendering run the same code
and a passing render says something about the real engine.

