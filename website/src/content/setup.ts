/*  How to build dew, for a reader who has not cloned it.
 *
 *  Per .ai/rules/README.md, website/src/content/ owns the CLAIM and never the
 *  argument: what the commands are, not why the dependency pins are split in
 *  two. README.md's Build, Run and Test sections are the same commands with the
 *  reasoning attached.
 *
 *  Two copies of a build command is the shape of thing that rots - one of them
 *  gets a new flag and the other does not, and the wrong one is the one a
 *  stranger runs. So `tests/setup.test.ts` reads ../README.md and fails if a
 *  command here is not in it, character for character. That is the same answer
 *  the C++ tree gives: a gate rather than a promise.
 */

export interface Requirement {
  readonly name: string;
  readonly need: string;
}

/** From the Brewfile, which is what `brew bundle` reads. */
export const requirements: readonly Requirement[] = [
  {
    name: 'macOS 11 or newer',
    need: 'The deployment target for these commands. Windows and Linux use the same presets.',
  },
  {
    name: 'Xcode command line tools',
    need: 'The compiler, and the clang-format ./scripts/check.sh runs.',
  },
  { name: 'Homebrew', need: 'How the four below arrive, from the Brewfile.' },
  { name: 'cmake ≥ 3.25', need: 'JUCE 9 needs 3.22; CMakePresets v6 needs 3.25.' },
  { name: 'ninja', need: 'The generator every preset uses.' },
  { name: 'ccache', need: 'Optional. Caches compiled objects between builds.' },
  { name: 'lame', need: 'MP3 export only, driven as a child process. Optional.' },
];

export interface Preset {
  readonly name: string;
  readonly what: string;
}

export const presets: readonly Preset[] = [
  { name: 'dev', what: 'Debug, with tests' },
  { name: 'release', what: 'RelWithDebInfo. Build this for normal use' },
  { name: 'ci', what: 'release plus warnings-as-errors' },
  { name: 'asan', what: 'ci plus AddressSanitizer and UndefinedBehaviorSanitizer' },
  { name: 'tsan', what: 'ci plus ThreadSanitizer' },
  { name: 'dist', what: 'What a release is built from: no tests, universal on Apple' },
  { name: 'dist-windows', what: 'dist, built by the Visual Studio generator' },
  { name: 'offline', what: 'release from a warm dependency cache, no network' },
];

/** Every line here is asserted to appear in README.md. Keep them identical,
 *  including the trailing comment on the brew line - a command that is nearly
 *  the same as the one in the repository is worse than an obvious copy.
 */
export const buildCommands = [
  'brew bundle                    # cmake >= 3.25, ninja, ccache, lame',
  'cmake --preset release',
  'cmake --build --preset release',
] as const;

export const runCommands = ['open build/release/src/dew_artefacts/RelWithDebInfo/dew.app'] as const;

export const checkCommands = ['./scripts/check.sh'] as const;

export const repositoryUrl = 'https://github.com/janschupke/dew';
