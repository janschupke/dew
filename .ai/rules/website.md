# The website

`website/` is a Next.js + TypeScript site: what dew is, what it does, and the score
language's reference. It is checked by `./scripts/check-website.sh`, which
`./scripts/check.sh` runs. [Why it is this way](#why-it-is-this-way), below, is the
argument.

**No C++ gate looks at it.** The formatting step reads `find src tests tools`, and every
source gate walks `DEW_SOURCE_DIR`. So the site carries its own, in `website/tests/`, and
they are the only thing standing between it and a second design system.

## Generated, committed, never hand-edited

| File | Written by | Held by |
| --- | --- | --- |
| `website/src/generated/score-schema.json` | `dew_docs schema` | a test in `dew_tests`, and `cmp` against a second process in CI |
| `website/src/generated/design-tokens.json` | `dew_shot tokens` | the same pair |
| `website/src/generated/score-samples.json` | `dew_shot samples` | the same pair |
| `website/src/app/theme.generated.css` | `website/scripts/gen-theme.mjs` | `npm run theme:check` |
| `website/src/app/icon.svg` | `website/scripts/gen-theme.mjs` | the same check |
| `website/public/shots/*.png` | `./scripts/gen-shots.sh`, from the **release** build | **nothing byte-wise** — see below |

Regenerate a JSON file in the commit that changes what it comes from, or the suite fails
naming the file and the command. Freshness is a **test**, not `git diff --exit-code`: that
would need `check.sh` to build a tool, run it and write into the working tree before
diffing, and a gate that mutates the tree it is judging cannot run on a tree with work in
it.

## The screenshots are not reproducible, and are not gated on bytes

dew paints with the system typeface, so a shot's pixels are a function of the macOS version
and the installed fonts — and `dew_shot gallery` sizes itself to its laid-out content, so
even its *height* moves. A byte comparison would fail on somebody else's machine for a
reason with nothing to do with dew.

So they are regenerated **deliberately**: run `./scripts/gen-shots.sh` after a UI change
worth showing, and **look at them**. `tests/WebsiteShotTests.cpp` holds what survives —
each decodes, is the size it was asked for, and is not one flat colour.

## What the site may not do

- **No colour written by hand.** Every one comes from a token in `theme.generated.css`,
  which is generated from the palette the application paints with. An inline style may
  reference a `var(--…)` and may not carry a value.
- **No `dark:` variant and no `prefers-color-scheme`.** The site is dark, one palette,
  for the reason [design-system.md](design-system.md) gives: the emphasis transforms and
  the four lift rungs encode "less is darker", which inverts on a light ground.
- **No arbitrary value carrying a colour or a px/rem/em length.** Tailwind v4's
  `--color-*: initial` and `--spacing: initial` already delete the off-vocabulary
  utilities, so `bg-blue-500` emits nothing; the gate catches one written anyway.
- **`t()` takes a literal key.** Building one at runtime is a way to name a key the type
  does not have — the same rule `src/i18n/Strings.h` states.

## Two things that are the site's own, and say so

- **The reading type ladder and the leading**, in `theme.site.css`. `Tokens.h` is 11, 12,
  13, 15 and 20px — panel-chrome sizes measured against the boxes a caption sits in — and
  the site was built out of them, so `text-body` at 13px was its most-used class and its
  body copy was two rungs under what a page is read at. The site's own rungs are `fine`,
  `prose`, `lead` and the four headings, in a second file with a header comment rather
  than smuggled into the generated one. **Reading text takes a site rung**; the app's are
  for `/design/`, which is showing them.
- **The page rhythm**, same file. `space::xxl` is 24px and is the top of the application's
  scale, which is right for a panel gutter and was also the largest gap the site could put
  between two sections. `gutter`, `stack`, `section` and `band` start above where the
  application's scale stops.
- **A rung may not be named after a CSS keyword.** Every `--spacing-*` key feeds every
  Tailwind sizing family, `inline-size` among them, so `--spacing-block` generated an
  `inline-block` utility that beat `display: inline-block` and sized five nav links to
  40px. `tests/utilities.test.ts` holds the reserved list.
- **A custom `@utility` has no directional family.** `@utility border-hairline` defines
  that one class and nothing else: Tailwind derives `border-b-2` from `border-2` and
  derives nothing from a custom utility, so `border-b-hairline` emitted no CSS at all and
  the nav's rule, the footer's rule and every table row separator were absent rather than
  faint. `gen-theme.mjs` emits all seven sides of each stroke now, and the gate that holds
  it reads the **compiled stylesheet**, because at the level of source there was nothing
  wrong.
- **`Button`'s prop is `variant`, not `role`.** The vocabulary is `DewButton`'s, but `role`
  is a real ARIA attribute on an anchor. It renders `next/link` for an internal href and
  an anchor for an external one.

## The highlighting is not a grammar

`.ai/rules/score-language.md` requires that the highlighter is not a second grammar, and
the site has none at all: `dew_shot samples` runs the compiler's own `lang::tokenize` and
the editor's own `ScoreTokeniser::colourFor`, and the page paints spans over committed
text. A test asserts the rendered text is byte-identical to the source, which is what
catches a run with a wrong offset. **Do not add a TypeScript tokenizer** — a live
playground would want an Emscripten build of `dew_lang`, which links nothing and would
therefore be cheap.

## Setup, and the one duplicated fact

`/setup/` carries the build commands, which `README.md` also carries. That is the same
fact in two homes, and the exception is argued rather than assumed: a reader who has not
cloned anything cannot be sent to a file inside the clone. What holds it is
`tests/setup.test.ts`, which reads `../README.md` and fails if a command in
`src/content/setup.ts` is not in it character for character — and reads `CMakePresets.json`
and the `Brewfile` for the same reason. Change a command in one place and the suite names
the other.

## The gate skips without node

`check-website.sh` reports a skip and says how to fix it, rather than failing: dew is a C++
project and somebody who never opens the site should not be stopped by a missing runtime.
CI sets `DEW_REQUIRE_NODE=1`, where a silent skip would be invisible. `npm ci`, never
`npm install` — the second rewrites `package-lock.json` and dirties the tree.

## Why it is this way

`website/` is a Next.js site: what dew is, what it does, and the score language's
reference. `./scripts/check-website.sh` checks it and `./scripts/check.sh` runs that.

**The reference is generated, and that is the point.** `src/lang/Schema.h` has always
declared the whole language as one table, and its own comment says the reference manual
reads it — "so a key cannot exist without being completable and cannot be documented
differently from how it is checked". Until now nothing read it but the completion popup and
the editor's highlighter. `dew_docs` is the reader that was missing: it walks `schema()`
and writes JSON, and the site renders every block, key, value kind and mode from it. A key
added to `Schema.cpp` appears on the page with no page edit, and a page that stopped
rendering one fails a test naming it.

**The JSON is committed, not built.** So the site needs no C++ toolchain: its CI job runs
on a Linux runner in about a minute instead of waiting behind a JUCE build, and a host that
has never heard of CMake can serve the export. The cost is that the files can go stale,
which is what the freshness test and CI's second-process `cmp` are for. Freshness is a test
rather than `git diff --exit-code` because the manifest's trick — regenerate, then diff —
would mean `check.sh` writing into the working tree before judging it.

**Two emitters rather than one, because of the link line.** `dew_docs` links `dew_lang` and
nothing else, JUCE included, so it is a second place that library's zero-dependency claim is
proved rather than asserted. The design tokens live in `dew_design`, which publicly links
`dew_engine` — one tool emitting both would have put `juce_gui_basics` on the score
compiler's link line, which is the mistake `dew_render` already made once. So `dew_shot`
emits the tokens, beside the picture of them it already renders.

**It takes its colours from the application, not from a copy of them.** `dew_shot tokens`
reads `darkPalette()` and `scripts/gen-theme.mjs` turns that into Tailwind's `@theme` block.
The lifts are the interesting part: `juce::Colour::brighter` is per sRGB channel and
truncates where CSS `color-mix` rounds, so the composed colours are computed by that same
call and emitted, rather than re-derived in a browser. It also settles a trap — a hovered
button is `surfaceRaised` lifted by `controlLift`, which is *not* `colour::surfaceHover`.

The theme's namespace resets do the work a source gate does in the C++ tree:
`--color-*: initial` deletes Tailwind's own palette and `--spacing: initial` its dynamic
scale, so `bg-blue-500` and `p-7` are not wrong — they do not exist. That is a build that
cannot express an off-vocabulary class, rather than a test that catches one.

**It is dark, one palette.** Both of dew's palettes are dark for a stated reason, and the
site inherits it: there is no `dark:` variant anywhere and a gate refuses one.

**Nothing highlights anything.** The rule that the highlighter is not a second grammar is
absolute, and a TypeScript tokenizer would have been a genuine third implementation. The
site renders committed samples and never user input, so it does not need one: `dew_shot
samples` scans with `lang::tokenize` and classifies with `ScoreTokeniser::colourFor`, and
the page paints spans. A test asserts the rendered text is byte-identical to the source.

**The screenshots are committed and are not gated on their bytes.** dew paints with the
system typeface, so a shot depends on the macOS version and the installed fonts — a `cmp`
would fail on somebody else's machine for a reason with nothing to do with dew. They are
regenerated deliberately with `./scripts/gen-shots.sh`; a test holds that each decodes, is
the size it was asked for, and is not one flat colour.

