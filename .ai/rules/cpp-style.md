# C++ style

C++20, `CXX_EXTENSIONS OFF`. macOS deployment target 11.0. **`.clang-format` is the
authority on layout** — run it, do not argue with it, and do not restate its settings here.
`./scripts/check.sh` fails on an unformatted file before it builds anything.

What the formatter cannot enforce:

- **`SortIncludes: Never`.** Include blocks are grouped by hand: the file's own header
  first, then JUCE, then dew, then the standard library, blank line between groups. A gate
  refuses the same header twice, and another refuses a relative cross-layer include — see
  [architecture.md](architecture.md).
- **Hand-aligned tables are fenced** with `// clang-format off` / `// clang-format on`.
  Several catalogs and token tables read as tables on purpose.
- Header comments carry the *why*. dew's convention is a long explanatory block at the top
  of a file or a declaration, stating what was there before and why it was replaced —
  match the density of the file you are editing rather than adding bare declarations.

## JUCE traps that compile clean

- **`juce::String` decodes a `const char*` as ASCII in its constructor and as UTF-8 in
  `operator+=`.** So with any non-ASCII literal:

  ```cpp
  someString + "  ·  "        // OK    — goes through operator+=
  "  ·  " + someString        // WRONG — builds String (const char*) first, ASCII
  juce::String ("  ·  ")      // WRONG — same constructor
  ```

  The broken form renders as `Â·`. **Always append a literal to an existing `String`**;
  never put one on the left of the first `+`, never wrap one in `juce::String (...)`. A
  gate refuses a concatenation that *starts* with a non-ASCII literal, and a UI string test
  can assert the text contains no `0xc2`.
- **`juce::dsp::WindowingFunction`'s third argument is `normalise = true`**, so it already
  divides out its own coherent gain. The textbook `4 / N` Hann scale then double-compensates
  and a full-scale sine reads +6dB. Either pass `false` and keep the 0.5, or keep the
  default and use `2 / N`. Calibrate on a bin centre — scalloping loss otherwise hides it.
- **`Component::findChildWithID` is not recursive**, and
  **`PopupMenu::MenuItemIterator` keeps a reference**. Both bite in tests; see
  [testing.md](testing.md).

## Standard-library limits on this deployment target

- **`std::from_chars` for doubles is unavailable below macOS 26.** Hand-roll it; `dew_lang`
  already has `src/lang/Numbers.*`.
- **`-Wswitch-enum` demands every enumerator** even with a `default:` present. For a
  wide value enum, dispatch on a name rather than switching.
- **`-Wfloat-equal` fires through Catch2's decomposer** under the `ci` preset. Use
  `juce::exactlyEqual`.

There is no manual `delete` anywhere in the tree; keep it that way — it is why the
sanitizer job runs no leak check.
