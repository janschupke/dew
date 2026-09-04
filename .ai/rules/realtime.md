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
- Pool slots key on each effect's **persistent id** by open addressing
  (`src/engine/EffectModulePool.*`), not on position. Positional keying renumbers later
  slots when an earlier chain changes and cuts the reverb tail they were in the middle of.
  `EffectTests.cpp` guards this; verify a change to it by making the assignment positional
  and confirming the test fails.
- Switching a slot's type is `reset()`, not a reallocation — the pool holds all effect
  types at max size, so topology travels in the snapshot like everything else.

## Three queues, three different contracts

`PreviewQueue` (single-producer ring), `SnapshotBridge` (triple-buffered publish through
one compare-and-swap) and `SignalTap` (overwrite-without-asking, `std::atomic<float>`
slots, one monotonic count) each answer a different question, and none of the three is a
default choice. Read [README.md](../../README.md#three-queues-three-different-contracts)
before touching any of them or adding a fourth.

`ctest --preset tsan` exists for exactly these three, and is **CI-only** — the TSan runtime
does not work on this machine. A race here looks like a flake rather than a failure.

The engine knows nothing about audio devices: it is prepared with a sample rate and a
block size and fills a buffer, which is why live playback and offline rendering run the
same code and a passing render says something about the real engine. Keep it that way.
