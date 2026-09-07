# The score language

`dew_lang` (`src/lang/`, namespace `dew::lang`) compiles `.score` text to real patterns,
notes and clips in a project. CLI: `dew_score`. In-app: the Score tab, Command-R.
[README.md](../../README.md#the-score-language) is the annotated example; the site's
`/score/reference/` is the generated reference, and [Why it is this
way](#why-it-is-this-way) below is the reasoning.

## Determinism is the whole point

- **`dew_lang` links nothing at all**, JUCE included. Keep it that way: a source gate
  asserts it knows nothing of JUCE, and `scripts/linux-check.sh` builds it under gcc and
  libstdc++ in Docker for one claim — that the same score compiles to the same notes under
  a second standard library. It is also why the language has its own string catalogue and
  its own message formatter rather than calling `tr` — see [i18n.md](i18n.md).
- **The compiler holds no mutable global state**, and a locale is not the place to start.
  `DiagnosticBag` takes one, `compile` and `completionsAt` take one defaulting to the
  reference, and nothing caches it: two compiles of the same score in one process must not
  be able to differ by something that changed between them.
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

- **A score binds a channel BY NAME.** `channel kick` finds the default project's "Kick"
  case-insensitively, so the demo builders' content names are join keys rather than labels
  and are never translated. A new project made in another language names its channels
  otherwise, and a score written against English then creates a channel instead of adopting
  one — the same behaviour as any other name mismatch. [i18n.md](i18n.md) owns that.
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
- **A grid test needs a score with `1/16` and `1/8t`** (lcm 12). `examples/rhodes.score`
  is the one that has both, and is therefore the one the harnesses compile; the other
  three land on grid 4 and prove nothing about it.
- **A tempo counts BEATS, and in a compound or odd metre a beat is an eighth.** `meter 6/8`
  at `tempo 208` is a dotted quarter of about 69, not anything anybody taps at 208. Writing
  a compound piece at the tempo its dotted quarter suggests makes it three times too slow,
  which is a thing to check with `dew_score --summary` rather than by listening.
- **A score should not clip when compiled with default sounds.** All four shipped scores
  did, at 1.19 to 1.39, because four voices at a written velocity of 60-plus into four
  identical saws is a hot mix. Velocities are written for the DEFAULT instrument; the demo
  that dresses the score stages its own levels afterwards.

## Why it is this way

### Determinism

`seed` is the root of a tree of keys, and every random choice draws from a stream derived
from its own **structural path** — section, instance, channel, site, bar, onset — never
from a byte offset and never from a shared stream. So editing one section leaves every
other section's notes bit-identical, inserting a blank line changes nothing, and adding a
`choose` at bar 3 cannot rewrite bar 4. `variance 0` is a pure argmin: the same notes
every compile, from any seed.

A melody can also be told how far it may jump and whether a jump has to be answered
(`leap max 7 resolve step`), and whether its rhythm restarts at each bar line or runs on
against it (`align bar` / `align continuous`). A voicing can be told which note goes at
the bottom — `bass from-inversion` is the default and is what makes writing `i^1` move a
note rather than decorate the page.

splitmix64 and PCG32 are written out rather than delegated. `std::uniform_int_distribution`
and `std::shuffle` specify their *statistics*, not their algorithms, so libstdc++ and
libc++ render different music from one seed; `juce::Random` is an LCG whose exact sequence
would become part of the file format. A test pins literal outputs, because those numbers
are now the format.

### The shape of it

`src/lang/` is `dew_lang`, a static library that links **nothing at all**, JUCE included —
a compiler that cannot reach into the document is one whose only output is its IR. A source
gate enforces it, because a header-only include would still link. The parser is hand-written
recursive descent; a generator would have cost a pinned dependency, a manifest row and a
Java-at-build-time decision for fifteen productions, and would have made byte-accurate
diagnostics harder rather than easier. Same argument as "Why not vcpkg or Conan" below.

Output is **baked** into the project as ordinary channels, patterns, notes and
`kind="pattern"` clips, so playback, the piano roll, the renderer, stems and MIDI export
all work on it unchanged and no new clip kind exists to be taught to the four places that
would need it. The language owns notes, patterns and clips; the user owns channels,
instruments, effects and the mixer — a track adopts a channel by name and reads nothing
from it but the name, so a sound you dialled in survives a recompile.

### Counterpoint, and choices a seed makes

A part may answer another rather than being written on its own:

```
part answer {
  counterpoint against lead {
    rhythm               pulse
    parallel-fifths      forbid
    parallel-octaves     forbid
    voice-crossing       forbid
    dissonance-on-strong soft 3
    leaps                soft 1.5
  }
}
```

A **beam search of width 8** over the onsets, scored against the voices already
written — not a constraint solver. A solver's failure modes are "unsatisfiable" and
"twenty seconds", both fatal in an editor that recompiles as you type; the cases a beam
loses are close to inaudible next to the machinery; and a beam's choice can be explained
in a diagnostic. Cost is additive along the timeline, so the beam is an exact dynamic
program over the states it keeps.

The seven rules are a closed set, each `forbid` or `soft <weight>` — closed because
completion depends on it, and because an open-ended rule language is a solver by another
name. When the hard rules leave nothing to sing they are **given up in a declared order,
one at a time, and every one is reported by bar**. The line never falls silent without
saying so. `species` is deliberately absent: Fux's rules are the easy fifth of it, and a
number in the language would imply a guarantee this cannot make.

**Imitation is an operator, not a search target:**

```
part echo {
  imitate lead {
    delay     1 bar
    transpose 2
    mode      diatonic     // stays in the key; `chromatic` moves exactly
  }
}
```

A beam search will essentially never *discover* imitation, because imitation constrains
the whole line's identity rather than local transitions — so asking a search for it is
asking for the one thing it cannot do. Written out it is exact, and it is fifteen lines.
Anything falling past the section's end is dropped rather than wrapped: a canon that
wrapped would answer itself from the future.

**A value can be chosen rather than set**, and say how often it is re-drawn:

```
cadence  choose [1 3 5] per instance   // a different ending in each verse
velocity 80 +- 20 per bar
```

The scope *is* the identity of the draw, not a knob on how random it is: `per song`
derives one key for the whole song, `per instance` one for each rendered instance, `per
bar` and `per note` go deeper. Same seed tree, different depth. `choose` takes a list and
nothing computable — the moment a value can be *computed*, completion stops being a table
lookup and the grid stops being statically knowable.

### The editor

Two things happen in the Score tab and they are deliberately not the same thing.
**Checking** runs on a debounce as you type: it lexes, parses, resolves and generates, and
it writes nothing - no notes, no undo entry. **Compiling** happens only when you ask, and
writes patterns, notes and clips in one undo transaction. A debounced auto-compile would
put an undo step full of notes on every pause in typing and would replace hand edits
without being asked, which is the one thing the recompile policy exists to prevent. The
source text itself *is* saved on the debounce, one transaction per typing run.

Errors surface in three places doing three jobs: the squiggle says **where**, the list
under the editor says **what** and scrolls the editor to it when clicked, and the status
bar says whether the project was written to at all.

Control-Space completes. Keys and block keywords come from the same schema table the
resolver validates against, names from the same resolve the compiler runs, and chords
through the same `resolveChord` that writes the notes — so nothing can be offered that the
compiler would then reject, and a key cannot be added without being completable. Chords are
ranked by the key that is written and **spelled beside the numeral**: in A minor `bVI`
reads `F`.

The highlighter is not a second grammar. `lang::scanOne` is a template over a minimal
cursor concept, and the editor's tokeniser is its second instantiation - the first walks a
`std::string_view` for the compiler, this one walks a `juce::CodeDocument::Iterator`. A
test asserts both produce the same token kinds over the example score, because a
highlighter that disagrees with the compiler is worse than none. Keywords are coloured from
the schema table rather than from a keyword list, so a key cannot exist without being
highlighted.

Compiling into a project that already has music refuses a grid or meter mismatch, as
described below. Compiling into an *empty* one applies both: nothing there has a meaning
they could change, and a new project sits at four steps per beat.

### Recompiling, and what happens to what you changed

The source lives **inside** the `.dew`, one node per line so it reads as a diff rather than
as one enormous string. A project and the score it came from are one document; the moment
they can travel separately, "which of these two files is current" becomes a question
somebody has to answer.

So compiling twice is an update, not a second copy. Every node a compile writes carries a
`genId` naming the part of the score that produced it — a section plus either its
occurrence number or its `as` label, the same identity the random draws use, so what pins
an instance's music also pins its document node. A pattern also carries the hash its notes
had when they were written. Recompiling hashes them again, which sorts every generated
pattern into three:

| State | What happens |
|---|---|
| hash matches | nobody has touched it — replaced |
| hash differs | edited in the piano roll — **kept**, counted, and reported |
| no longer produced | removed, unless it was edited, in which case it stays without a clip |

`dew_score --discard-edits` takes the other branch. Both are one undo transaction either
way. The hash covers the length and the notes and deliberately not the name: renaming a
pattern is not a musical change, and letting it read as one would mean labelling a pattern
quietly stopped the compiler ever updating it again.

The honest limit: a clip on the generated lane is rebuilt every time, because where a
section sits is the arrangement's to say. Drag one to another lane and it is yours.

