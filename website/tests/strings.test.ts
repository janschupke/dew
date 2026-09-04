import { describe, expect, it } from 'vitest';
import { readFileSync } from 'node:fs';

import en from '@/messages/en.json';
import { t } from '@/lib/strings';
import { sourceFiles, withoutComments } from './scan';

/** Every dotted path to a string leaf, walked at runtime.
 *
 *  The type does this at compile time; this is the same walk as data, so a
 *  test can answer every message without naming any of them.
 */
function leaves(node: unknown, prefix = ''): string[] {
  if (typeof node === 'string') return [prefix];
  if (typeof node !== 'object' || node === null) return [];

  return Object.entries(node).flatMap(([key, value]) =>
    leaves(value, prefix ? `${prefix}.${key}` : key),
  );
}

describe('strings', () => {
  const ids = leaves(en);

  it('has a catalogue to be a gate over', () => {
    expect(ids.length).toBeGreaterThan(30);
  });

  it('every key is lowerCamel segments joined by dots', () => {
    // The shape GenStrings.cmake enforces, kept here even though nothing on
    // this side mangles anything. It costs nothing, and it means a key can
    // move between the two catalogues without a rename.
    for (const id of ids) expect(id).toMatch(/^[a-z][a-zA-Z0-9]*(\.[a-z][a-zA-Z0-9]*)*$/);
  });

  it('every message resolves with no placeholder left standing', () => {
    // The web analogue of GenStrings.cmake recording argument names so a test
    // can answer a message without knowing what it says: extract the names the
    // same way, supply a stub for each, and assert no `{` survives.
    for (const id of ids) {
      const raw = String(id.split('.').reduce<never>((n, s) => n[s], en as never));

      const args = Object.fromEntries(
        [...raw.matchAll(/\{\s*([a-z][a-zA-Z0-9]*)/g)]
          .map((m) => m[1] ?? '')
          .filter((name) => !['zero', 'one', 'two', 'few', 'many', 'other'].includes(name))
          .map((name) => [name, name === 'count' ? 2 : `<${name}>`]),
      );

      const answered = t(id as Parameters<typeof t>[0], args);

      expect(answered, `${id} left a placeholder: ${answered}`).not.toMatch(/[{}]/);
      expect(answered.length).toBeGreaterThan(0);
    }
  });

  it('a plural picks its branch and substitutes the count', () => {
    expect(t('reference.kindsCount', { count: 1 })).toBe('1 value kind');
    expect(t('reference.kindsCount', { count: 31 })).toBe('31 value kinds');
  });

  it('every key the catalogue declares is one the site asks for', () => {
    // The mirror of the type. `t('nav.referrence')` is a compile error, so a
    // key that is NAMED is certainly a key that EXISTS - and nothing at all
    // held the other direction. `nav.source` sat in this file unused for as
    // long as it had been there, which is how a catalogue starts carrying
    // sentences nobody has read.
    //
    // This is tests/SourceGateStringsTests.cpp's "every string the catalogue
    // declares is one the app asks for", on this side of the tree.
    const asked = new Set<string>();

    for (const file of sourceFiles()) {
      const code = withoutComments(readFileSync(file, 'utf8'));

      for (const match of code.matchAll(/\bt\(\s*'([^']+)'/g)) asked.add(match[1] ?? '');
    }

    // It cannot pass by finding nothing.
    expect(asked.size).toBeGreaterThan(20);

    const unused = ids.filter((id) => !asked.has(id));

    expect(unused.join('\n')).toBe('');
  });

  it('an unknown key returns its own path, never empty', () => {
    // The rule src/i18n/Strings.h follows, so a gap is visible on the page
    // rather than a blank nobody notices.
    const missing = 'nav.nothingLikeThis' as Parameters<typeof t>[0];

    expect(t(missing)).toBe('nav.nothingLikeThis');
  });
});
