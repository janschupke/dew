import { describe, expect, it } from 'vitest';

import en from '@/messages/en.json';
import { t } from '@/lib/strings';

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

  it('an unknown key returns its own path, never empty', () => {
    // The rule src/i18n/Strings.h follows, so a gap is visible on the page
    // rather than a blank nobody notices.
    const missing = 'nav.nothingLikeThis' as Parameters<typeof t>[0];

    expect(t(missing)).toBe('nav.nothingLikeThis');
  });
});
