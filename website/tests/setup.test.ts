import { describe, expect, it } from 'vitest';
import { readFileSync } from 'node:fs';

import { brewedTools, checkCommands, platforms, presets, repositoryUrl } from '@/content/setup';

/*  The setup page and the repository are the same instructions twice.
 *
 *  A reader who has not cloned anything cannot be sent to a file inside the
 *  clone, so the site has to carry the commands. What must not happen is the
 *  two drifting, because the copy a stranger runs is the one nobody who works
 *  on this repository ever reads. So the second copy is held against the
 *  first, character for character.
 */
const readme = readFileSync('../README.md', 'utf8');
const brewfile = readFileSync('../Brewfile', 'utf8');
const ci = readFileSync('../.github/workflows/ci.yml', 'utf8');
const presetsJson = JSON.parse(readFileSync('../CMakePresets.json', 'utf8')) as {
  configurePresets: { name: string; hidden?: boolean }[];
};

const commands = [
  ...platforms.flatMap((platform) => [...platform.build, ...platform.run]),
  ...checkCommands,
];

describe('the setup page says what the repository says', () => {
  it('quotes every command out of README.md verbatim', () => {
    expect(commands.length).toBeGreaterThan(8);

    for (const command of commands) expect(readme, command).toContain(command);
  });

  it('covers all three platforms', () => {
    // dew is built and its suite is run on all three in CI, and the page said
    // "macOS 11 or newer" and left the other two to a parenthetical.
    expect(platforms.map((platform) => platform.system).sort()).toEqual([
      'linux',
      'macos',
      'windows',
    ]);

    for (const platform of platforms) {
      expect(platform.build.length, platform.system).toBeGreaterThan(0);
      expect(platform.run.length, platform.system).toBe(1);
      expect(platform.requirements.length, platform.system).toBeGreaterThan(2);
    }
  });

  it('names every configure preset the repository has, and no other', () => {
    // A preset added to CMakePresets.json and not to the page is a build the
    // site does not know about; one on the page and not in the file is a
    // command that fails.
    const real = presetsJson.configurePresets
      .filter((preset) => preset.hidden !== true)
      .map((preset) => preset.name)
      .sort();

    expect(presets.map((preset) => preset.name).sort()).toEqual(real);
  });

  it('names only tools the Brewfile installs', () => {
    expect(brewedTools.length).toBeGreaterThan(2);

    for (const name of brewedTools) expect(brewfile, name).toContain(`brew "${name}"`);
  });

  it('asks for the Linux packages CI installs, and no others', () => {
    // The README's own argument for pointing at this file: a list CI runs is a
    // list that is true. A package here that CI does not install is one nobody
    // has ever proved is enough.
    const linux = platforms.find((platform) => platform.system === 'linux');
    const install = linux?.build.find((line) => line.includes('apt install')) ?? '';

    const packages = install
      .replace(/^sudo apt install /, '')
      .split(/[\s\\]+/)
      .filter((name) => name !== '');

    expect(packages.length).toBeGreaterThan(10);

    for (const name of packages) expect(ci, `${name} is not in ci.yml`).toContain(name);
  });

  it('points at the repository the README points at', () => {
    expect(readme).toContain(repositoryUrl);
  });
});
