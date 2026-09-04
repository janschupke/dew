import { describe, expect, it } from 'vitest';

import { arbitraryValues, offenders, report, sourceFiles, unownedMeasurement } from './scan';

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

  it('no arbitrary value carrying a measurement the ladder should own', () => {
    // `bg-[#2b2f36]` and `p-[13px]` are how the closed vocabulary gets
    // reopened. See unownedMeasurement in ./scan for what is refused and why
    // three things are not - the predicate is shared with the control case
    // below so the two cannot drift.
    const found = offenders(/-\[[^\]]+\]/).filter((o) =>
      arbitraryValues(o.text).some(unownedMeasurement),
    );

    expect(report(found)).toBe('');
  });

  it('sees an arbitrary measurement when there is one', () => {
    expect(unownedMeasurement('13px')).toBe(true);
    expect(unownedMeasurement('#2b2f36')).toBe(true);
    expect(unownedMeasurement('240px')).toBe(true);
    expect(unownedMeasurement('1.5rem')).toBe(true);

    expect(unownedMeasurement('auto_1fr')).toBe(false);
    expect(unownedMeasurement('--motion-quick-ms')).toBe(false);
    expect(unownedMeasurement('68ch')).toBe(false);
    expect(unownedMeasurement('72rem')).toBe(false);
  });

  it('no spacing off the ladder', () => {
    // The seven rungs are 2, 4, 6, 8, 12, 16, 24 and deliberately not a strict
    // 4px grid. `p-3` and `gap-7` are Tailwind's dynamic scale, which the
    // theme's `--spacing: initial` already deletes - this catches one written
    // anyway, where it would silently emit nothing.
    expect(report(offenders(/\b(?:p|m|gap|space)[trblxy]?-\d/))).toBe('');
  });

  it('no positional offset off the ladder', () => {
    // `--spacing: initial` deletes the scale every numeric offset reads, not
    // just the padding the gate above refuses - so `top-0` compiles to
    // `top: calc(var(--spacing) * 0)`, which is invalid and drops.
    //
    // The header was `sticky top-0 z-10` from the day it was written and never
    // stuck: it had a position and no offset, and `top: auto` on a sticky box
    // scrolls away like a static one. Nothing at the level of source was wrong,
    // which is why this reads the source for the CLASS of mistake instead.
    // theme.site.css declares `pinned` for the one place that needs it.
    expect(report(offenders(/\b(?:top|bottom|left|right|inset(?:-[xy])?)-\d/))).toBe('');
  });

  it('sees a positional offset when there is one', () => {
    const pattern = /\b(?:top|bottom|left|right|inset(?:-[xy])?)-\d/;

    expect(pattern.test('className="sticky top-0"')).toBe(true);
    expect(pattern.test('className="absolute inset-0"')).toBe(true);
    expect(pattern.test('className="inset-x-4"')).toBe(true);

    // Neither of these reads the spacing scale, and both are in use.
    expect(pattern.test('className="pinned z-10"')).toBe(false);
    expect(pattern.test('className="last:border-0"')).toBe(false);
    expect(pattern.test('className="grid sm:grid-cols-2"')).toBe(false);
  });

  it('no inline style carrying a colour of its own', () => {
    // An inline style is allowed to reference a TOKEN and nothing else.
    //
    // The design page needs one: a swatch's colour is chosen by the name it is
    // iterating over, and Tailwind cannot generate `bg-${name}` because a class
    // has to be statically visible to be emitted. `var(--color-...)` in a style
    // attribute is the right answer there - it is still the token, resolved at
    // runtime rather than at build time.
    //
    // What is refused is an inline style holding a VALUE: that is a colour with
    // no owner, which is the whole point of the hex gate above.
    const found = offenders(/style=\{\{[^}]*(?:color|background|border)/i).filter(
      (o) => !o.text.includes('var(--'),
    );

    expect(report(found)).toBe('');
  });

  it('sees an inline style with a value in it', () => {
    const literal = (text: string) =>
      /style=\{\{[^}]*(?:color|background|border)/i.test(text) && !text.includes('var(--');

    expect(literal("style={{ backgroundColor: '#2b2f36' }}")).toBe(true);
    expect(literal('style={{ borderColor: theme.accent }}')).toBe(true);
    expect(literal('style={{ backgroundColor: `var(--color-accent)` }}')).toBe(false);
  });
});
