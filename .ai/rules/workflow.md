# Workflow

## The gate

```sh
./scripts/check.sh              # --no-build when the tree is already built
```

Dependency pins, the generated Cursor rule, formatting, a warnings-as-errors build, the
tests, and the generated manifest — in the order that fails cheapest first. **Run it before
calling anything done.** `ctest --preset release` is *not* the gate: only `ci` builds
warnings-as-errors. See [testing.md](testing.md).

Presets are in `CMakePresets.json`; do not pass raw flags. `dev` for debugging (libc++
hardening bounds-checks `operator[]`), `release` for normal use, `ci` for the gate.

Outside the gate, and **CI-only** — neither sanitizer runtime works on this macOS:

```sh
ctest --preset asan
ctest --preset tsan
./scripts/linux-check.sh        # dew_lang under gcc and libstdc++, in Docker
```

## `build/ci` is not the app

The user opens
`build/release/src/dew_artefacts/RelWithDebInfo/dew.app`. **After any UI change, also run
`cmake --build --preset release`** — otherwise the feature is invisible however green the
gate is. Verify a visual change with `dew_shot`, not by reasoning about the code.

## Generated, committed, never hand-edited

| File | Written by | Held by |
| --- | --- | --- |
| `THIRD_PARTY.md` | CMake, at configure time | `git diff --exit-code` in `check.sh` |
| `examples/*` | `dew_render --write-demos examples` | tests pinning file == binary == factory |
| `presets/*` | `dew_render --write-presets presets` | tests walking the committed list against `PresetFactory`, both ways |
| `.cursor/rules/main.mdc` | `./scripts/gen-cursor-rules.sh`, from `AGENTS.md` | `--check`, in `check.sh` |

`.cursor/rules/main.mdc` is checked locally only — it is not a CI job, so an `AGENTS.md`
edit committed without regenerating will pass CI and fail the next local gate. Run the
generator in the same commit.

Binary-data source lists in `src/CMakeLists.txt` are listed explicitly and **never
globbed**, so a file added to `examples/` must be named there too.

## Dependencies

CPM, vendored at `cmake/CPM.cmake` and sha256-verified. Every package is pinned to a 40-hex
commit in `cmake/DependencyPins.cmake` and locked in `cpm-package-lock.cmake`;
`scripts/check-deps.sh` refuses a floating ref (`master`, `HEAD`, `origin/*`) and a package
with no pin. Adding a dependency means a pin, a lock entry and a `ThirdPartyManifest` row.
The only external tool is `lame`, driven as a child process for MP3 encoding — no build
dependency and no licence question.

## Git

- **Never create a branch.** Commit to the branch you are already on.
- **Commit subjects are imperative sentence-case prose with no prefix** — "Let the mixer
  hold as many inserts as the music needs", "Make the gate one command, and run it before
  the build". No `feat:`, no `fix:`. The body explains the defect and the alternatives
  that were rejected.
- Commit before reporting that something is done.
- A formatting-only commit goes in `.git-blame-ignore-revs`.

## Dates

Use the `date` CLI when writing one into a file. ISO 8601.
