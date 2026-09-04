import { describe, expect, it } from 'vitest';
import { readFileSync } from 'node:fs';

import { checksumsAsset, downloadUrl, downloads, releasesUrl } from '@/content/download';
import { repositoryUrl } from '@/content/setup';

/*  The download page's claims, held against the workflow that produces them.
 *
 *  This is tests/setup.test.ts's argument in a second place: the page and the
 *  release are the same fact twice, and the copy a stranger clicks is the one
 *  nobody who works on this repository ever uses. A renamed artefact would
 *  otherwise leave a button that 404s, and nothing would say so until somebody
 *  pressed it.
 */
const workflow = readFileSync('../.github/workflows/release.yml', 'utf8');

describe('the download page offers what the release workflow uploads', () => {
  it('has downloads to be a gate over', () => {
    // It cannot pass by finding nothing.
    expect(downloads.length).toBeGreaterThan(3);
    expect(workflow.length).toBeGreaterThan(1000);
  });

  it('names an asset the workflow uploads, for every platform', () => {
    for (const download of downloads)
      expect(workflow, `${download.asset} is on the page and not in release.yml`).toContain(
        `dist/${download.asset}`,
      );
  });

  it('names the checksums file the workflow writes', () => {
    expect(workflow).toContain(checksumsAsset);
  });

  it('covers all three platforms', () => {
    const platforms = downloads.map((d) => d.platform.toLowerCase()).join(' ');

    for (const system of ['macos', 'windows', 'linux']) expect(platforms).toContain(system);
  });

  it('says what the first launch does, for every download', () => {
    // The page exists to stop somebody meeting an unsigned-binary dialog cold.
    // An entry with nothing to say here is one that would.
    for (const download of downloads)
      expect(download.firstLaunch.length, download.asset).toBeGreaterThan(20);
  });
});

describe('the links do not need a version', () => {
  it('every asset link is a latest/download link on the repository', () => {
    for (const download of downloads)
      expect(downloadUrl(download.asset)).toBe(
        `${repositoryUrl}/releases/latest/download/${download.asset}`,
      );
  });

  it('no asset name carries a version number', () => {
    // The whole reason the links are stable. A version in a file name means the
    // page has to know which version is current, which means either a fetch the
    // site refuses to make or a generated file that is briefly wrong after
    // every bump.
    for (const download of downloads)
      expect(download.asset, `${download.asset} carries a version`).not.toMatch(/\d+\.\d+/);
  });

  it('points at the releases page for older versions', () => {
    expect(releasesUrl).toBe(`${repositoryUrl}/releases`);
  });
});
