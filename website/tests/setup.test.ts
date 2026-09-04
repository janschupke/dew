import { describe, expect, it } from 'vitest';
import { readFileSync } from 'node:fs';

import {
  buildCommands,
  checkCommands,
  presets,
  repositoryUrl,
  requirements,
  runCommands,
} from '@/content/setup';

/*  The setup page and the README are the same instructions twice.
 *
 *  That is the arrangement the repository's own rule about one fact and one
 *  home would normally forbid - and the exception is argued rather than
 *  assumed: a reader who has not cloned anything cannot be sent to a file
 *  inside the clone, so the site has to carry the commands. What must not
 *  happen is the two drifting, because the copy a stranger runs is the one
 *  nobody who works on this repository ever reads.
 *
 *  So the second copy is held against the first, character for character. This
 *  is the same answer the C++ tree gives when documentation has to stay true:
 *  ScoreBakeTests compiles the language example out of the README rather than
 *  trusting it.
 */

const readme = readFileSync('../README.md', 'utf8');
const brewfile = readFileSync('../Brewfile', 'utf8');
const presetsJson = JSON.parse(readFileSync('../CMakePresets.json', 'utf8')) as {
  configurePresets: { name: string; hidden?: boolean }[];
};

describe('the setup page says what the repository says', () => {
  it('quotes every command out of README.md verbatim', () => {
    const commands = [...buildCommands, ...runCommands, ...checkCommands];

    expect(commands.length).toBeGreaterThan(4);

    for (const command of commands) expect(readme, command).toContain(command);
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
    // The four that come from brew. The first three requirements are the
    // machine rather than a package, so they are not expected here.
    const brewed = requirements
      .map((requirement) => requirement.name)
      .filter((name) => /^[a-z]+$/.test(name));

    expect(brewed.length).toBeGreaterThan(2);

    for (const name of brewed) expect(brewfile, name).toContain(`brew "${name}"`);
  });

  it('points at the repository the README points at', () => {
    expect(readme).toContain(repositoryUrl);
  });
});
