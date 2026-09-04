import { describe, expect, it } from 'vitest';

import { offenders, report, sourceFiles } from './scan';

describe('source gates', () => {
  // Control case, first and separately. Every gate below passes silently when
  // it finds nothing, which is indistinguishable from finding nothing because
  // the walk broke - the exact argument tests/SourceGateTests.cpp makes about
  // checking its own scan.
  it('has sources to be a gate over', () => {
    const files = sourceFiles();

    expect(files.length).toBeGreaterThan(10);
    expect(files).toContain('src/ui/Button.tsx');
    expect(files.some((f) => f.endsWith('.css'))).toBe(true);
  });

  it('no colour is written by hand', () => {
    // The analogue of the app's hex gate. Every colour on this site comes from
    // a token in theme.generated.css, which is generated from the palette the
    // application paints with - so a hex here is a second, unowned copy.
    const found = offenders(/#[0-9a-fA-F]{3,8}\b|\b(?:rgba?|hsla?|oklch)\s*\(/);

    expect(report(found)).toBe('');
  });

  it('sees a colour when there is one', () => {
    // The gate can see. Without this it is green because it is blind, which is
    // how a source gate fails in practice.
    const pattern = /#[0-9a-fA-F]{3,8}\b|\b(?:rgba?|hsla?|oklch)\s*\(/;

    expect(pattern.test('className="bg-[#ff0000]"')).toBe(true);
    expect(pattern.test('color: rgb(1 2 3)')).toBe(true);
    expect(pattern.test('className="bg-surface-raised"')).toBe(false);
  });

  it('no dark: variant anywhere', () => {
    // dew is dark, one palette, and that is a decision rather than an
    // omission: the emphasis transforms and all four lift rungs encode "less
    // is darker", which inverts on a light ground. A dark: variant here would
    // be the beginning of a light theme nothing else supports.
    expect(report(offenders(/\bdark:/))).toBe('');
  });

  it('no prefers-color-scheme branch', () => {
    expect(report(offenders(/prefers-color-scheme/))).toBe('');
  });

  it('no arbitrary value that is not a token reference', () => {
    // `bg-[#2b2f36]` and `p-[13px]` are how the closed vocabulary gets
    // reopened. A bracket holding a var() or a custom property is a token by
    // another spelling and is allowed; a bracket holding a NUMBER is not.
    //
    // max-w-[68ch] and max-w-[72rem] are the measure and the page width. They
    // are the site's own layout rather than anything Tokens.h names, and there
    // is no rung for either - a text column is measured in characters.
    const allowed = /\[(?:--|var\(|68ch|72rem)/;
    const found = offenders(/-\[[^\]]+\]/).filter((o) => !allowed.test(o.text));

    expect(report(found)).toBe('');
  });

  it('no spacing off the ladder', () => {
    // The seven rungs are 2, 4, 6, 8, 12, 16, 24 and deliberately not a strict
    // 4px grid. `p-3` and `gap-7` are Tailwind's dynamic scale, which the
    // theme's `--spacing: initial` already deletes - this catches one written
    // anyway, where it would silently emit nothing.
    expect(report(offenders(/\b(?:p|m|gap|space)[trblxy]?-\d/))).toBe('');
  });

  it('no inline style carrying a colour', () => {
    expect(report(offenders(/style=\{\{[^}]*(?:color|background|border)/))).toBe('');
  });
});
