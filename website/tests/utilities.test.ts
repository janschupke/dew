import { describe, expect, it } from 'vitest';
import { readFileSync } from 'node:fs';

import { offenders, sourceFiles, withoutComments } from './scan';

/*  Every class this site writes is one the theme actually defines.
 *
 *  This gate exists because of a bug no other kind could have found. Tailwind
 *  v4's `@utility border-hairline` defines one class and no family: unlike the
 *  built-in `border-2`, from which `border-b-2` is derived, a custom utility
 *  has no directional variants. So `border-b-hairline` and `border-t-hairline`
 *  were written in the nav, the footer and both reference tables, and emitted
 *  NOTHING - the rules under the header, above the footer and between every
 *  table row were not faint, they were absent.
 *
 *  Nothing could see it. The class was in the markup, the token was in
 *  theme.generated.css, the page rendered, the type-checker was happy and the
 *  gates that read the SOURCE all passed - because at the level of source there
 *  was nothing wrong. Only the compiled stylesheet knew, which is why this one
 *  reads the compiled stylesheet.
 *
 *  The same trap is waiting behind any future `@utility`, which is why this
 *  asks the general question rather than checking the borders.
 */

const theme = readFileSync('src/app/theme.generated.css', 'utf8');

/** Every class `@utility` declares, from the generated theme. */
const declared = new Set(
  [...theme.matchAll(/@utility\s+([A-Za-z0-9_-]+)\s*\{/g)].map((match) => match[1] ?? ''),
);

/** The prefixes those class names start with, so the check can tell a name
 *  meant to be a custom utility from an ordinary Tailwind class that happens to
 *  share a word. */
const families = new Set([...declared].map((name) => name.split('-')[0] ?? ''));

/** Every class written in the source, with variants (`hover:`, `sm:`) and
 *  opacity modifiers (`/50`) stripped back to the base name. */
function classesUsed(): { file: string; line: number; name: string }[] {
  const found: { file: string; line: number; name: string }[] = [];

  for (const file of sourceFiles()) {
    if (file.endsWith('.css')) continue;

    withoutComments(readFileSync(file, 'utf8'))
      .split('\n')
      .forEach((text, i) => {
        for (const match of text.matchAll(/'([^']*)'|"([^"]*)"|`([^`]*)`/g)) {
          const literal = match[1] ?? match[2] ?? match[3] ?? '';

          for (const token of literal.split(/\s+/)) {
            const base = (token.split(':').pop() ?? '').split('/')[0] ?? '';

            if (/^[a-z][a-z0-9-]*$/.test(base)) found.push({ file, line: i + 1, name: base });
          }
        }
      });
  }

  return found;
}

describe('the theme defines what the source writes', () => {
  it('declares a directional variant of every stroke rung', () => {
    // The four rungs times the seven forms. Stated as the shape rather than as
    // a count, because a fifth stroke is a token change and should not be a
    // test change too.
    for (const rung of ['whisper', 'hairline', 'regular', 'bold'])
      for (const prefix of ['', 't-', 'r-', 'b-', 'l-', 'x-', 'y-'])
        expect(declared, `border-${prefix}${rung}`).toContain(`border-${prefix}${rung}`);
  });

  it('emits a rule for every custom utility the source uses', () => {
    // A class that LOOKS like one of the custom families - it starts with the
    // same word - but is not declared. `border-divider` is Tailwind's own
    // border-color utility reading a --color-* key, so anything the theme does
    // not declare has to be something Tailwind can still generate; the two are
    // told apart by asking whether the rest of the name is a token.
    const strokes = ['whisper', 'hairline', 'regular', 'bold'];

    const missing = classesUsed().filter(({ name }) => {
      const family = name.split('-')[0] ?? '';
      if (!families.has(family)) return false;
      if (declared.has(name)) return false;

      // Only the stroke names are this file's business; a colour or a radius
      // reaches Tailwind's own utilities through an @theme key.
      return strokes.some((rung) => name.endsWith(`-${rung}`));
    });

    expect(missing.map((m) => `${m.file}:${String(m.line)}  ${m.name}`).join('\n')).toBe('');
  });

  it('catches a stroke class the theme does not declare', () => {
    // The control case. Without one this gate could be passing because it
    // matches nothing at all.
    const strokes = ['whisper', 'hairline', 'regular', 'bold'];
    const invented = 'border-tl-hairline';

    expect(declared.has(invented)).toBe(false);
    expect(strokes.some((rung) => invented.endsWith(`-${rung}`))).toBe(true);
    expect(families.has(invented.split('-')[0] ?? '')).toBe(true);
  });

  it('names no spacing rung after a CSS keyword a utility already ends in', () => {
    // The `inline-block` collision, as a rule.
    //
    // Every --spacing-* key feeds every sizing family Tailwind has, inline-size
    // among them. `--spacing-block` therefore generated `inline-block` meaning
    // `inline-size: 2.5rem`, which won against the built-in `display:
    // inline-block` and set every nav link to 40px wide whatever its text said.
    // Nothing at the level of source was wrong, so nothing at the level of
    // source could have caught it.
    //
    // These are the keywords that appear as the tail of a Tailwind utility in
    // one family and would be read as a token in another.
    const reserved = [
      'block',
      'flex',
      'grid',
      'table',
      'inline',
      'contents',
      'flow-root',
      'none',
      'auto',
      'full',
      'screen',
      'min',
      'max',
      'fit',
      'px',
    ];

    const site = readFileSync('src/app/theme.site.css', 'utf8');
    const rungs = [...site.matchAll(/--spacing-([a-z-]+)\s*:/g)].map((m) => m[1] ?? '');

    // It cannot pass by finding no rungs.
    expect(rungs.length).toBeGreaterThan(2);

    expect(rungs.filter((name) => reserved.includes(name))).toEqual([]);
  });

  it('scanned something', () => {
    expect(sourceFiles().length).toBeGreaterThan(10);
    expect(declared.size).toBeGreaterThan(10);
    expect(offenders(/never-matches-anything-at-all/)).toHaveLength(0);
  });
});
