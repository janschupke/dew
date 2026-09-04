import { render } from '@testing-library/react';
import { describe, expect, it } from 'vitest';

import { Score } from '@/ui/Score';
import { samples } from '@/lib/samples';

const roles = ['plain', 'keyword', 'literal', 'string', 'comment', 'punctuation', 'invalid'];

describe('score samples', () => {
  it('has samples to be a gate over', () => {
    expect(samples.length).toBeGreaterThan(2);

    for (const s of samples) {
      expect(s.source.length).toBeGreaterThan(500);
      expect(s.runs.length).toBeGreaterThan(100);
    }
  });

  it('renders each sample byte for byte', () => {
    // The assertion that makes this site a viewer rather than a highlighter.
    // The runs come from the compiler's own lang::tokenize and the gaps between
    // them are emitted verbatim, so the rendered text must equal the source
    // exactly - a run with a wrong offset or length shows up here and nowhere
    // else.
    for (const s of samples) {
      const { container } = render(<Score name={s.name} />);

      expect(container.querySelector('code')?.textContent).toBe(s.source);
    }
  });

  it('every run carries a role the stylesheet knows', () => {
    for (const s of samples) for (const run of s.runs) expect(roles).toContain(run.role);
  });

  it('every role but invalid actually occurs', () => {
    // The control case. A tokeniser that had silently collapsed to one role
    // would render, would reproduce the source, and would be uniformly the
    // wrong colour - which nothing above would catch.
    const seen = new Set(samples.flatMap((s) => s.runs.map((r) => r.role)));

    for (const role of roles.filter((r) => r !== 'invalid')) expect(seen).toContain(role);

    // And no example score is a lexical error.
    expect(seen).not.toContain('invalid');
  });

  it('runs never overlap and are in order', () => {
    for (const s of samples) {
      let at = 0;

      for (const run of s.runs) {
        expect(run.offset).toBeGreaterThanOrEqual(at);
        expect(run.length).toBeGreaterThan(0);
        at = run.offset + run.length;
      }

      expect(at).toBeLessThanOrEqual(s.source.length);
    }
  });

  it('an excerpt colours as the whole file does at those lines', () => {
    const first = samples[0];

    if (!first) throw new Error('no samples');

    const excerpt = first.source.split('\n').slice(4, 9).join('\n');
    const { container } = render(<Score name={first.name} lines={[5, 9]} />);

    expect(container.querySelector('code')?.textContent).toBe(excerpt);
  });
});
