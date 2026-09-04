# The score language

`dew_lang` (`src/lang/`, namespace `dew::lang`) compiles `.score` text to real patterns,
notes and clips in a project. CLI: `dew_score`. In-app: the Score tab, Command-R.
[README.md](../../README.md#the-score-language) is the reference for the language itself.

## Determinism is the whole point

- **`dew_lang` links nothing at all**, JUCE included. Keep it that way: a source gate
  asserts it knows nothing of JUCE, and `scripts/linux-check.sh` builds it under gcc and
  libstdc++ in Docker for one claim — that the same score compiles to the same notes under
  a second standard library.
- **Never `std::uniform_int_distribution`, never `std::shuffle`, never `juce::Random`.**
  The standard fixes their statistics, not their algorithms, so libstdc++ and libc++
  render different music. Use the hand-written splitmix64 and PCG32 in `src/lang/Rng.h`.
- Draws are **stateless and keyed on the structural path** — a section name plus its
  occurrence number or its `as` label — never on a byte offset. That is the same identity
  `PatternDesc::key` and the document's `genId` carry, which is why what pins an instance's
  music also pins its document node. Never key on the pattern's *name*: a user may edit it.
- **The compiler must be byte-identical across PROCESSES.** Two compiles in one process
  agree on the same rubbish: an inconsistent comparator once let a sort read past the end
  of its range, and twelve runs of `dew_score` produced three different projects while the
  in-process determinism suite stayed green. The guard that catches this class is the CI
  step that compiles in a **second process** and `cmp`s. Keep it, and add to it rather
  than replacing it with an in-process test.

## Three walls the host imposes

Reject what these forbid by naming the **host** limitation, not as a grammar error:

1. **Time is integer steps.** A note is `{ch, step, lengthSteps, pitch, velocity}` and
   `stepsPerBeat` runs 1..16. The required grid is an lcm over the duration literals; `1/32`
   plus any triplet needs 24, which is unrepresentable — the error must name **both**
   witnesses.
2. **One tempo, one meter, project-wide.** A 3/4 section inside a 4/4 project is not
   expressible. Never apply a meter change to work around it: `ProjectEdits::setMeter`
   rescales every clip.
3. **A pattern holds every channel's notes for its span.** So section → pattern,
   arrangement entry → clip, on one playlist lane. `x2 identical` is one pattern and two
   clips; a re-rolled repeat is two patterns.

Compiling into a project that already has music **refuses** a grid or meter mismatch.
Compiling into an empty one **applies** both — a fresh project sits at four steps per beat
and nothing there has a meaning they could change.

## Settled — do not relitigate

One statement per line. No expression language at all, which is what makes completion a
table lookup and lets the grid be computed statically. Seven closed block keywords.
Comments are `//`, because `#` is a sharp. Output is **baked** into the ValueTree as real
notes — never a new clip kind. Source text lives inside the `.dew`, one node per line.
There is **no `rebakeInto`**: `into` is the idempotent path with a policy defaulting to
`keepHandEdits`, so one code path is always exercised. `species`, the literal `voice [...]`
escape hatch, and `--json` golden files were cut and stay cut.

## Traps

- **An accidental reads against the MAJOR scale; a bare numeral reads against the mode.**
  Flattening the mode's own degree is right in major (`bVII` = B♭ in C) and a semitone low
  in minor, where the sixth is already flat. Every minor-key score once rendered a semitone
  below what it said.
- **A thing that produces nothing looks like success from every angle.** Three separate
  bugs wrote no notes and reported nothing. A part that writes no notes now warns (W604),
  and the example test asserts every track has notes. Keep both.
- **The highlighter is not a second grammar.** `ui/ScoreTokeniser` instantiates
  `lang::scanOne` over a `juce::CodeDocument::Iterator`; keywords come from
  `lang::schema()`, never a list. A test asserts both instantiations produce the same
  token kinds.
- **`juce::CodeDocument::Iterator` claims it is not at EOF after the final newline** and
  then returns nothing. A cursor's `isEOF` must mean "nothing left to read" and must not
  fold that in with "not ASCII".
- **Write a velocity as an exact 3-decimal double.** `addNote` takes a float; 0.535f widens
  to 0.53500001430511475, the serializer writes six decimals, and the project fails to
  round-trip while its JSON stays byte-identical.
- **`std::from_chars` for doubles is unavailable below macOS 26.** Hand-roll it —
  `src/lang/Numbers.*`.
- **A grid test needs a score with `1/16` and `1/8t`** (lcm 12). `examples/amber.score`
  needs grid 4, so it proves nothing about the grid.
