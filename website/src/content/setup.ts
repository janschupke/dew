/*  How to build dew, for a reader who has not cloned it.
 *
 *  Two copies of a build command is the shape of thing that rots, so
 *  tests/setup.test.ts reads ../README.md and fails if a command here is not in
 *  it, character for character.
 */
import type { PlatformSystem } from '@/ui/Icon';

export interface Requirement {
  readonly name: string;
  readonly need: string;
}

export interface Platform {
  readonly system: PlatformSystem;
  /** The heading, and what it covers. */
  readonly name: string;
  readonly what: string;
  readonly requirements: readonly Requirement[];
  /** A note the commands do not carry, or an empty string. */
  readonly note: string;
  readonly build: readonly string[];
  readonly run: readonly string[];
}

/** One command, wrapped the way a shell wraps one, so it can be read as well
 *  as pasted. It is the list .github/workflows/ci.yml installs, minus what only
 *  a headless runner needs. */
const linuxPackages = [
  'sudo apt install build-essential ninja-build git pkg-config \\',
  '  libasound2-dev libfreetype-dev libfontconfig1-dev \\',
  '  libx11-dev libxext-dev libxinerama-dev libxrandr-dev libxcursor-dev \\',
  '  libxcomposite-dev libxrender-dev libxi-dev \\',
  '  libgl1-mesa-dev libglu1-mesa-dev mesa-common-dev',
].join('\n');

export const platforms: readonly Platform[] = [
  {
    system: 'macos',
    name: 'macOS',
    what: 'macOS 11 or newer, Apple Silicon or Intel.',
    requirements: [
      {
        name: 'Xcode command line tools',
        need: 'The compiler, and the clang-format the gate runs.',
      },
      { name: 'Homebrew', need: 'How the rest arrive, from the Brewfile in the repository root.' },
      { name: 'cmake ≥ 3.25', need: 'JUCE 9 needs 3.22; CMakePresets v6 needs 3.25.' },
      { name: 'ninja', need: 'The generator every preset uses.' },
      { name: 'ccache', need: 'Optional. Caches compiled objects between builds.' },
      { name: 'lame', need: 'Optional, and only for MP3 export.' },
    ],
    note: 'brew bundle reads the Brewfile in the repository root and installs the four tools below the first two.',
    build: [
      'brew bundle                    # cmake >= 3.25, ninja, ccache, lame',
      'cmake --preset release',
      'cmake --build --preset release',
    ],
    run: ['open build/release/src/dew_artefacts/RelWithDebInfo/dew.app'],
  },
  {
    system: 'linux',
    name: 'Linux',
    what: 'x86_64, glibc 2.35 or newer. Built and tested on Ubuntu 22.04.',
    requirements: [
      { name: 'gcc or clang', need: 'C++20. build-essential is enough on Debian and Ubuntu.' },
      {
        name: 'cmake ≥ 3.25',
        need: 'Ubuntu 22.04 ships 3.22, which is too old for CMakePresets v6 — take a newer one from cmake.org.',
      },
      { name: 'ninja', need: 'The generator every preset uses.' },
      {
        name: 'ALSA, X11, freetype, fontconfig, mesa',
        need: 'The development packages JUCE links against, installed by the command below.',
      },
      { name: 'lame', need: 'Optional, and only for MP3 export.' },
    ],
    note: 'The package list is the one CI installs, so it is a list that is true. webkit2gtk and libcurl are deliberately absent: dew builds with JUCE_WEB_BROWSER=0 and JUCE_USE_CURL=0.',
    build: [linuxPackages, 'cmake --preset release', 'cmake --build --preset release'],
    run: ['./build/release/src/dew_artefacts/RelWithDebInfo/dew'],
  },
  {
    system: 'windows',
    name: 'Windows',
    what: 'Windows 10 or 11, x64.',
    requirements: [
      {
        name: 'Visual Studio 2022',
        need: 'With the Desktop development with C++ workload. The generator finds this toolchain itself.',
      },
      { name: 'cmake ≥ 3.25', need: 'The installer offers it; Visual Studio also ships one.' },
      { name: 'lame.exe', need: 'Optional, and only for MP3 export. It has to be on PATH.' },
    ],
    note: 'The Visual Studio generator rather than Ninja, which would need a developer command prompt to find cl.exe.',
    build: ['cmake --preset dist-windows', 'cmake --build --preset dist-windows'],
    run: ['build\\dist-windows\\src\\dew_artefacts\\RelWithDebInfo\\dew.exe'],
  },
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

/** Every line here is asserted to appear in README.md. */
export const checkCommands = ['./scripts/check.sh'] as const;

/** What the macOS block installs from brew, for the test that holds this file
 *  against the Brewfile. */
export const brewedTools = ['cmake', 'ninja', 'ccache', 'lame'] as const;

export const repositoryUrl = 'https://github.com/janschupke/dew';
