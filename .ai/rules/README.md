# Rules

What each rule file is for is in [AGENTS.md](../../AGENTS.md); what follows is the rule
about the rules.

## One fact, one home

**The surface that owns a fact is the one that argues it. Every other mention is a
sentence and a link.**

The cost of ignoring this is not tidiness. A fact copied into four files gets corrected in
one or two of them and left wrong in the rest, and the wrong copies are indistinguishable
from the right ones. dew has already paid this once in code: `cutoff` stopped at 18kHz in
two tables and 20kHz in a third, and a mixer fader offered 0–1.5 against an engine clamp
of 2.0, which is why a parameter is one `ParamSpec` row now.

| Surface | Owns | Never |
| --- | --- | --- |
| Inline comment | Why *this code* is shaped this way — a JUCE trap, a measurement local to the file, an alternative deleted here | Restating the README's argument. Link to it. |
| `README.md` | The reasoning: what dew is, why the layers are libraries, why there is no command queue, why animation is off by default. dew has no `docs/adr/` — this is where a decision is argued | Telling you what to do before writing code. That is a rule's job. |
| `.ai/rules/` | What you must do before writing code, and what will fail if you do not | Reasoning at length. Name the README section instead. |
| `THIRD_PARTY.md` | Nothing. CMake writes it at configure time | Being hand-edited — `scripts/check.sh` diffs it |
| `AGENTS.md` | An index, and the excerpt an agent trips over first | Anything not derivable from the files it links |
| `.cursor/rules/main.mdc` | Nothing. It is generated from `AGENTS.md` | Being edited by hand — `./scripts/gen-cursor-rules.sh --check` fails |
| `CLAUDE.md` | Nothing. One line, `@AGENTS.md` | Growing content of its own |

## Numbers in prose rot, so prefer one that cannot

Three kinds, and they are not the same:

- **A measurement that argued a decision** — the six wheel speeds `Gestures.h` replaced,
  the thirty of sixty-three controls the help-text gate found silent. These were true when
  the decision was made and the decision stands on them, so keep them, but only while
  nothing else in the tree states a different number for the same thing. When the code
  beside a measurement disagrees with it, the code wins and the prose is wrong.
- **A live count of the code** — how many tests, how many icons, how many gates, how many
  source files. These rot within a commit or two. Do not write one into a rule; say "a
  gate" and name the test, not "twenty-three gates".
- **A configured value** — a token, a pin, a preset flag, a deployment target. These have
  exactly one home already, which is the file that declares them. Point at it rather than
  quoting it: `Tokens.h`, `cmake/DependencyPins.cmake`, `CMakePresets.json`.

## What is enforced, and by what

dew ships **no linter and no `.clang-tidy`**. Everything beyond `clang-format` is enforced
by a test. Before adding a rule to one of these files, ask which of the three it is:

- **A source gate** if the rule is about what the text of a source file may say —
  `tests/SourceGateTests.cpp` scans `DEW_SOURCE_DIR` and fails as a test. This is the
  answer for almost every convention here: no hex colour, no bare radius, no key outside
  the registry, no relative cross-layer include. A gate reports what it found and names
  the file, which prose cannot.
- **A link error** if the rule is about what a layer may depend on. That is why the layers
  are separate static libraries at all, and why `dew_lang` links nothing.
- **A behavioural test** if it needs more than one file to agree — the editor and the
  engine evaluating the same curve at 201 steps, the committed `examples/` compared byte
  for byte against what the factory writes, CI compiling a score in a second process and
  `cmp`-ing the result.

Reach for prose only when none of the three applies. A gate added is worth more than a
paragraph added, and a gate must have a **control case** — one over an empty list is not a
gate, and `SourceGateTests` asserts its own scan against the libraries' compiled source
lists so code that moves out of `src/` cannot quietly disarm it.
