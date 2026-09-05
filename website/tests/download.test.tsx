import { render } from '@testing-library/react';
import { describe, expect, it } from 'vitest';
import { readFileSync } from 'node:fs';

import Download from '@/app/download/page';
import {
  checksumsAsset,
  downloadUrl,
  downloads,
  primaryDownloads,
  releasesUrl,
} from '@/content/download';
import { repositoryUrl } from '@/content/setup';
import { systemName } from '@/content/systems';

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

describe('every download says which system it is for', () => {
  const systems = ['macos', 'windows', 'linux'] as const;

  it('keys each entry to one of the three', () => {
    // A closed key rather than a match against the platform SENTENCE. The page
    // used to have only the sentence, so a mark, a button or a grouping could
    // only have been chosen by searching "Windows, portable" for a word - which
    // goes wrong silently the first time that text is edited.
    for (const download of downloads)
      expect(systems, `${download.asset} has no system`).toContain(download.system);

    for (const system of systems)
      expect(
        downloads.map((download) => download.system),
        `nothing is offered for ${system}`,
      ).toContain(system);
  });

  it('offers one primary download per system, in the order the list declares', () => {
    // The page used to carry a single primary button hardcoded to downloads[0],
    // which handed a macOS disk image to everybody and left two readers in
    // three to find their own row.
    expect(primaryDownloads.map((download) => download.system)).toEqual([...systems]);

    for (const download of primaryDownloads)
      expect(downloads.indexOf(download), `${download.asset} is not the first of its system`).toBe(
        downloads.findIndex((other) => other.system === download.system),
      );
  });

  it('draws a mark on every primary button and every row', () => {
    // The mark is aria-hidden everywhere, so what is asserted is that it is
    // DRAWN - the accessible name is the words beside it, in all three places.
    const { container } = render(<Download />);

    for (const download of primaryDownloads) {
      const button = [...container.querySelectorAll('a')].find(
        (link) =>
          link.getAttribute('href') === downloadUrl(download.asset) &&
          link.textContent.includes(systemName(download.system)),
      );

      expect(button, `no button for ${download.system}`).toBeDefined();
      expect(button?.querySelector('svg'), `${download.system} button has no mark`).not.toBeNull();
    }

    for (const row of container.querySelectorAll('tbody tr'))
      expect(row.querySelector('svg'), row.textContent).not.toBeNull();

    expect(container.querySelectorAll('tbody tr')).toHaveLength(downloads.length);
  });
});
