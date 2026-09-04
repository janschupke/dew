# The website

`website/` is a Next.js + TypeScript site: what dew is, what it does, and the score
language's reference. [README.md](../../README.md#the-website) argues why it is shaped this
way. It is checked by `./scripts/check-website.sh`, which `./scripts/check.sh` runs.

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

- **The display type rungs**, in `theme.site.css`. `type::display` is 20px because the
  largest text in a DAW is a panel heading; a landing page needs a hero. They are in a
  second file with a header comment rather than smuggled into the generated one.
- **`Button`'s prop is `variant`, not `role`.** The vocabulary is `DewButton`'s, but `role`
  is a real ARIA attribute on an anchor.

## The highlighting is not a grammar

`.ai/rules/score-language.md` requires that the highlighter is not a second grammar, and
the site has none at all: `dew_shot samples` runs the compiler's own `lang::tokenize` and
the editor's own `ScoreTokeniser::colourFor`, and the page paints spans over committed
text. A test asserts the rendered text is byte-identical to the source, which is what
catches a run with a wrong offset. **Do not add a TypeScript tokenizer** — a live
playground would want an Emscripten build of `dew_lang`, which links nothing and would
therefore be cheap.

## The gate skips without node

`check-website.sh` reports a skip and says how to fix it, rather than failing: dew is a C++
project and somebody who never opens the site should not be stopped by a missing runtime.
CI sets `DEW_REQUIRE_NODE=1`, where a silent skip would be invisible. `npm ci`, never
`npm install` — the second rewrites `package-lock.json` and dirties the tree.
