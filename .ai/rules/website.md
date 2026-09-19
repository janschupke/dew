# The website

`website/` is a Next.js + TypeScript site: what dew is, what it does, and the generated
references for the score language and the MCP endpoint. It is checked by
`./scripts/check-website.sh`, which `./scripts/check.sh` runs.

**It is a product site.** What it may not carry is internal reasoning: no page says which
tool emitted it or which header a table came from, and nothing links into `.ai/rules/`.
The argument for a decision lives beside the code, not in front of a reader deciding
whether to try a DAW.

**No C++ gate looks at it.** The formatting step reads `find src tests tools`, and every
source gate walks `DEW_SOURCE_DIR`. So the site carries its own, in `website/tests/`, and
they are the only thing standing between it and a second design system.

## Generated, committed, never hand-edited

| File | Written by | Held by |
| --- | --- | --- |
| `website/src/generated/score-schema.json` | `dew_docs schema` | a test in `dew_tests`, and `cmp` against a second process in CI |
| `website/src/generated/design-tokens.json` | `dew_shot tokens` | the same pair |
| `website/src/generated/score-samples.json` | `dew_shot samples` | the same pair |
| `website/src/generated/mcp-tools.json` | `dew_mcp schema` | the same pair |
| `website/src/app/theme.generated.css` | `website/scripts/gen-theme.mjs` | `npm run theme:check` |
| `website/src/app/icon.svg` | `website/scripts/gen-theme.mjs` | the same check |
| `resources/icon/dew.svg` | `website/scripts/gen-theme.mjs` | the same check |
| `resources/icon/dew.png` | `dew_shot icon`, via `./scripts/gen-shots.sh` | `tests/IconTests.cpp`, against the SVG's two fills |
| `website/public/shots/*.png` | `./scripts/gen-shots.sh`, from the **release** build | **nothing byte-wise** — see below |

Regenerate a JSON file in the commit that changes what it comes from, or the suite fails
naming the file and the command. Freshness is a **test**, not `git diff --exit-code`: that
would need `check.sh` to build a tool, run it and write into the working tree before
diffing, and a gate that mutates the tree it is judging cannot run on a tree with work in
it.

The last two are the application's icon rather than the site's, and are in this table
because `gen-theme.mjs` is what writes them: the mark is the accent on the window colour,
so it comes from the palette like every other colour here. `juce_add_gui_app` wants
`ICON_BIG` to exist at **configure** time and `dew_shot` is a target in the same project,
so the PNG is committed rather than built.

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
- **`--spacing: initial` deletes every numeric OFFSET too, not only the padding.**
  The gate has always refused `p-3` and `gap-7`, and `top-0` is the same deletion
  wearing a different family: it compiles to `top: calc(var(--spacing) * 0)`, which
  is invalid and drops. The header was `sticky top-0 z-10` from the day it was
  written and never stuck once — it had a position and no offset, and `top: auto`
  on a sticky box scrolls away like a static one. `theme.site.css` declares
  `pinned` for the one place that needs it, and `tests/gates.test.ts` refuses a
  numeric offset anywhere.
- **A `Card` with an `href` is a link and looks like one STANDING STILL.** Every card
  on the site used to hover and go nowhere: a pointer answered by a colour change and
  a click answered by nothing. Where there is a destination the card is a real
  `next/link`, which is also how it gets the focus ring for free; where there is none
  — the MCP page's four, the features page's leftovers — the hover is gone rather than
  pointing somewhere invented. The hover alone was not enough: the two were the same
  box until you pointed at one, so a link now sits at `surface-raised` with the strong
  divider and an accent arrow, and a panel at `surface` with the whisper stroke and no
  arrow. `tests/pages.test.tsx` asserts both directions over the home page.

- **The home page hero is a rule, `.hero` in `theme.site.css`, not a `Container`.** The
  text column wants the page's centred 72rem column and the picture wants the space
  outside it, running off the right edge of the window; one element cannot be both.
  The rule reproduces the column's left edge with
  `max(gutter, (100vw - 72rem) / 2 + gutter)` — the arithmetic
  `mx-auto max-w-[72rem] px-gutter` performs — and its two columns are `68ch` and what
  is left, which are the only two lengths `tests/scan.ts` allows written out. `Shot`
  takes a `flush` prop for it: a frame closed on four sides would put a rounded corner
  half off screen. It is a plain rule because Tailwind v4 cannot nest an `@utility`
  in a media query.

- **The two state changes a reader sees are animated, and nothing else is.** A shot's
  dialog fades and rises on `--motion-panel-ms`, and its `::backdrop` dims AND blurs.
  The EXIT animates without moving `close()` out of the click handler:
  `transition-behavior: allow-discrete` on `display` and `overlay` keeps the dialog in
  the top layer for the length of the transition, and `@starting-style` supplies the
  "before" a dialog otherwise has none of. A route change is `RouteFade` — a
  `'use client'` wrapper keyed on `usePathname()`, so a navigation remounts it and
  restarts one keyframe animation. Next's `experimental.viewTransition` is an unstable
  flag over a React API this tree is not on, and a bare `@view-transition` never fires
  because the App Router navigates without swapping the document. Both are dropped
  under `prefers-reduced-motion: reduce`, which is a query about MOTION and not the
  `prefers-color-scheme` branch the gate refuses.

- **Blur is the site's own rung.** `--blur-veil` in `theme.site.css`, beside the type
  and rhythm ladders and for the same reason: the application paints panels on an
  opaque ground and has nothing behind one to defocus, so `Tokens.h` has no opinion
  and the generated file must not grow a value nothing in dew paints.
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
- **A shot opens full-screen, in a native `<dialog>`.** `ShotViewer` is one of the site's
  four `'use client'` components, with `Nav`, `TocNav` and `RouteFade`. `showModal()` buys the top layer — so
  it cannot lose to the pinned header's `z-10` — plus Escape, a focus trap and an inert
  background, all of which hand-rolled would be a second implementation of something the
  platform has. Its box and its `::backdrop` are plain rules in `globals.css`, because
  `fixed inset-0` is a numeric offset and the gate refuses one. **jsdom implements neither
  `showModal` nor `close`**, so `tests/setup.ts` supplies both and `tests/shot.test.tsx`
  asserts that shim is in place before asserting anything else — the wiring is covered on
  this side and the top-layer behaviour is not.
- **A sectioned page's sidebar is generated from the same table its sections are.** `Toc`
  is the shell — `/features/`, `/score/`, `/mcp/` and the three generated reference pages
  all wear it. Each group carries a `data-toc-group`, and the tests assert **both
  directions per group**: every section has an entry, and every entry names a section that
  exists. A count over the whole nav would go green the day a block was dropped and a
  table added.
- **`TocNav` is the sidebar's client half, and marks what is on screen.** It takes the
  LAST anchor whose top has passed `--spacing-section` — the rung every `scroll-mt-section`
  clears — and falls back to the first when none has. The offset is read from the
  stylesheet rather than written in the component, so the two cannot drift.
  `tests/toc.test.tsx` stubs `getBoundingClientRect`, because jsdom has no layout.
- **A multi-page section carries a `pages` group.** `/score/` and `/mcp/` are sections, not
  loose pages: the header marks the section by longest prefix and the sidebar's first group
  says which page of it you are on. `src/content/sections.ts` holds those lists.
- **The nav is `src/content/nav.ts`, once.** The header and the footer read the same array;
  they used to hold it twice, by hand. The footer's four columns are `footerColumns` there,
  and the flat `footerLinks` the tests read is DERIVED from them — one seven-row list beside
  a two-row one is a shape, not a menu. Home is matched EXACTLY and everything else by
  prefix, because `/` is a prefix of every other href.
- **`docked` is the sidebar's sticky, and `pinned` the header's.** Same reason for both:
  the numeric offset a `sticky top-<n>` needs reads the scale `--spacing: initial`
  deletes, and emits nothing. `docked` sits at `--spacing-section`, which is the rung
  `scroll-mt-section` already uses to clear the pinned header.
- **The MCP resources are a page, not a section.** `/mcp/resources/` carries the five
  documents `dew_mcp` emits from `control::guide()`; `/mcp/reference/` carries the tools.
  They were one page, with five essays between the tool index and the tools. The anchors
  did not move — only the path in front of them.
- **The download page keys its platform marks off `Download.system`.** A closed
  `'macos' | 'windows' | 'linux'`, not a search of the platform sentence: "Windows,
  portable" is a sentence a reader reads and an edit away from breaking a match. `Icon.tsx`
  holds all four marks; every one is `currentColor` and `aria-hidden`, because the hex
  gate refuses a literal fill and the words are already beside it.

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
`src/content/setup.ts` is not in it character for character. Change a command in one place
and the suite names the other.

**All three platforms, each complete on its own.** `src/content/setup.ts` is a list of
`Platform` records — requirements, build, run — keyed off the same closed
`'macos' | 'windows' | 'linux'` union `/download/` uses for its marks. A Linux reader must
never have to read the macOS block to know what to run.

The rest of the same file is held against the repository too: the preset table against
`CMakePresets.json`, `brewedTools` against the `Brewfile`, and the Linux apt line against
the list in `.github/workflows/ci.yml` — because a list CI runs is a list that is true.

## The legal page

`/terms/` is `src/content/terms.ts`, and `tests/terms.test.ts` holds it against `LICENSE`
and `THIRD_PARTY.md`: the licence it names has to be the one in the repository, and every
library the build resolved has to be mentioned. A dependency added to `cpm-package-lock.cmake`
appears in the generated `THIRD_PARTY.md` and fails that test until the page says so.

**dew is AGPLv3, which is copyleft and not permissive.** The page says both halves — free
for any purpose including commercially, and an offer of corresponding source owed to
anybody given a binary or reached over a network. Half of that is worse than neither.

## The gate skips without node

`check-website.sh` reports a skip and says how to fix it, rather than failing: dew is a C++
project and somebody who never opens the site should not be stopped by a missing runtime.
CI sets `DEW_REQUIRE_NODE=1`, where a silent skip would be invisible. `npm ci`, never
`npm install` — the second rewrites `package-lock.json` and dirties the tree.

## Deploying, and the setting no file can hold

`website/vercel.json` is the deployment: `npm ci`, `npm run build`, publish `out/`. It sits
in `website/` and not at the repository root because **the Vercel project's Root Directory
is `website`**, and Vercel reads `vercel.json` from the root directory — a file above it is
never opened.

That one setting lives in the dashboard and nowhere else, because it cannot live anywhere
else: Vercel has to resolve the root directory before it can find a config file inside it.
It is written down here and in the README instead, which is the closest a repository gets
to owning it.

It has already cost a deployment. A `vercel.json` at the root carrying
`cd website && npm ci` looked right and was never read; what ran was the dashboard's copy
of the same string, from a working directory that was **already** `website/`, so it looked
for `website/website` and the install failed. Nothing was ever published, so every path
including `/` served Vercel's own 404 — which is why the site appeared to have neither an
index nor a not-found page when it has always had both.

`tests/deploy.test.ts` refuses a `cd` in any of those commands and refuses a `vercel.json`
at the repository root, and `check-website.sh` fails if a build does not leave
`out/index.html` and `out/404.html` behind. `framework` is `null` rather than `"nextjs"`
for the reason `next.config.ts` gives at length: the export needs no server, and naming a
framework invites one.
