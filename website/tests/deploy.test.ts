import { describe, expect, it } from 'vitest';
import { existsSync, readFileSync } from 'node:fs';

/*  The deployment, held to the shape it actually runs in.
 *
 *  Vercel's Root Directory for this project is `website`, so every command runs
 *  with this directory as the working directory and Vercel reads `vercel.json`
 *  from here. That setting lives in the dashboard and cannot live anywhere else
 *  - Vercel has to resolve the root directory before it can find a config file
 *  inside it - so this file is where the repository holds the consequences of
 *  it instead.
 *
 *  Both mistakes below have happened, together, and cost the whole site:
 *  a `vercel.json` at the REPOSITORY root carrying `cd website && npm ci`. The
 *  file was never read, and the dashboard's copy of the same string ran from a
 *  working directory that was already `website/` - so it looked for
 *  `website/website`, the install failed, and nothing was ever published. Every
 *  path including `/` then served Vercel's own 404, which is indistinguishable
 *  from a site with no index and no not-found page unless you read the copy.
 *
 *  What a build LEAVES BEHIND is not checked here: `out/` exists only after
 *  `next build`, and the suite runs before it. `check-website.sh` checks that.
 */

/** A deployment command that changes directory. Under Root Directory =
 *  `website` there is nowhere to go: the build is already where it belongs. */
const changesDirectory = (command: string) => /(?:^|[;&|]\s*)cd\s/.test(command);

interface VercelConfig {
  framework: string | null;
  installCommand: string;
  buildCommand: string;
  outputDirectory: string;
}

const config = JSON.parse(readFileSync('vercel.json', 'utf8')) as VercelConfig;
const commands = [config.installCommand, config.buildCommand];

describe('the Vercel deployment', () => {
  // Control case, first and separately. Every gate below reads `config`, and a
  // gate over a file that parsed to nothing useful is green because it is blind.
  it('has a config to be a gate over', () => {
    expect(commands.every((command) => typeof command === 'string' && command.length > 0)).toBe(
      true,
    );
    expect(config.outputDirectory).toBeTypeOf('string');
  });

  it('no command changes directory', () => {
    expect(commands.filter(changesDirectory)).toEqual([]);
  });

  it('sees a command that changes directory when there is one', () => {
    expect(changesDirectory('cd website && npm ci')).toBe(true);
    expect(changesDirectory('npm ci && cd website')).toBe(true);

    expect(changesDirectory('npm ci')).toBe(false);
    expect(changesDirectory('npm run build')).toBe(false);

    // `cd` inside a word is not a command. Without this the gate would refuse
    // any script whose name happens to contain the letters.
    expect(changesDirectory('npm run build:cdn')).toBe(false);
  });

  it('publishes the directory the export writes', () => {
    // Relative to THIS directory, because that is Vercel's working directory.
    // `website/out` is the same mistake as the `cd` above, spelled as a path.
    expect(config.outputDirectory).toBe('out');

    // And the export still writes it. Two halves of one fact, in two files.
    expect(readFileSync('next.config.ts', 'utf8')).toMatch(/output:\s*'export'/);
  });

  it('names no framework', () => {
    // next.config.ts argues it at length: a static export has no job for a
    // server, and naming a framework invites Vercel to provision one.
    expect(config.framework).toBeNull();
  });

  it('has no config above it, where nothing would read one', () => {
    expect(existsSync('../vercel.json')).toBe(false);
  });

  it('has the two pages a deployment is judged by', () => {
    // `/` and everything else. Vercel serves `out/404.html` for an unmatched
    // path, so not-found.tsx is what stands between a wrong URL and the
    // platform's own 404 - and the platform's is what the site served for as
    // long as the install was broken.
    expect(existsSync('src/app/page.tsx')).toBe(true);
    expect(existsSync('src/app/not-found.tsx')).toBe(true);
  });
});
