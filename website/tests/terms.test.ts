import { describe, expect, it } from 'vitest';
import { readFileSync } from 'node:fs';

import { licence, licenceShort, terms } from '@/content/terms';

/*  The legal page against the two files it describes. THIRD_PARTY.md is
    generated at configure time from what CMake actually resolved, so a
    dependency added to the build appears there first - and this fails until
    the page mentions it.
*/
const licenceText = readFileSync('../LICENSE', 'utf8');
const thirdParty = readFileSync('../THIRD_PARTY.md', 'utf8');
const prose = terms.flatMap((section) => section.paragraphs).join('\n');

describe('the terms page', () => {
  it('has sections to be a gate over', () => {
    expect(terms.length).toBeGreaterThan(4);

    for (const section of terms) expect(section.paragraphs.length, section.id).toBeGreaterThan(0);
  });

  it('names the licence the repository actually carries', () => {
    expect(licenceText.toUpperCase()).toContain(licence.toUpperCase());
    expect(prose).toContain(licence);
    expect(prose).toContain(licenceShort);
  });

  it('mentions every library the build resolved', () => {
    // The LIBRARIES table only. The file carries a second one below it
    // recording the toolchain that configured the build - CMake and the
    // compiler are not things a licence page has anything to say about.
    const table = thirdParty.split('## Build toolchain')[0] ?? '';
    const libraries = [...table.matchAll(/^\| ([A-Za-z][\w.+-]*) \| `/gm)].map(
      (match) => match[1] ?? '',
    );

    expect(libraries.length).toBeGreaterThan(1);

    for (const library of libraries) expect(prose, library).toContain(library);
  });

  it('states the two things a licence page is read for', () => {
    expect(prose).toMatch(/without warranty of any kind/i);
    expect(prose).toMatch(/any purpose/i);
  });

  it('describes the AGPL as what it is', () => {
    // It grants use for any purpose AND obliges an offer of source. Calling it
    // permissive would be wrong in the one place being wrong matters.
    expect(prose).not.toMatch(/permissive/i);
    expect(prose).toMatch(/corresponding source/i);
    expect(prose).toMatch(/network/i);
  });
});
