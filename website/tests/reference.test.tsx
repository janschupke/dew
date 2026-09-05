import { render } from '@testing-library/react';
import { describe, expect, it } from 'vitest';

import Reference from '@/app/score/reference/page';
import { anchorForBlock, anchorForKey, anchorForValueKind, schema } from '@/lib/schema';

const keyCount = schema.blocks.reduce((n, b) => n + b.keys.length, 0);

describe('the generated reference', () => {
  // Control case. Every assertion below walks the schema, and a walk over an
  // empty table passes while proving nothing - the failure tests/SourceGateTests.cpp
  // guards against by checking its own scan.
  it('has a schema to be a gate over', () => {
    expect(schema.blocks.length).toBeGreaterThan(10);
    expect(keyCount).toBeGreaterThan(40);
    expect(schema.valueKinds.length).toBeGreaterThan(25);
    expect(schema.valueKinds.filter((k) => k.members.length > 0).length).toBeGreaterThan(8);
    expect(schema.modes.length).toBeGreaterThan(10);
  });

  it('renders every block, with its doc', () => {
    const { container } = render(<Reference />);

    for (const block of schema.blocks) {
      const section = container.querySelector(`#${CSS.escape(anchorForBlock(block.kind))}`);

      expect(section, `block ${block.kind} has no section`).not.toBeNull();
      expect(section?.textContent).toContain(block.kind);

      // The doc string carries markdown code spans, which <Doc> splits on a
      // backtick - so compare against the text with the backticks removed.
      expect(section?.textContent).toContain(block.doc.replaceAll('`', ''));
    }
  });

  it('every block has a link in the sidebar', () => {
    // Found by trying to break the section gate above and watching it pass: the
    // first attempt dropped a block from the NAV, every assertion stayed green,
    // and the page shipped an index that silently omitted one. A gate over the
    // sections is not a gate over the list of them.
    //
    // Asked of the BLOCKS group rather than of the whole sidebar, which also
    // carries the value and mode tables now. A count over everything in the nav
    // would go green the day a block was dropped and a table added.
    const { container } = render(<Reference />);

    expect(container.querySelector('nav')).not.toBeNull();

    const targets = new Set(
      [...container.querySelectorAll('[data-toc-group="blocks"] a')].map((a) =>
        a.getAttribute('href'),
      ),
    );

    expect(targets.size).toBe(schema.blocks.length);

    for (const block of schema.blocks)
      expect(targets, `block ${block.kind} has no sidebar link`).toContain(
        `#${anchorForBlock(block.kind)}`,
      );
  });

  it('the sidebar names nothing the page does not carry', () => {
    // The other direction. The value and mode tables are reachable from the
    // sidebar and from nowhere else, so a renamed section id would be a dead
    // entry the browser mentions only to whoever clicked it.
    const { container } = render(<Reference />);

    const links = [...(container.querySelector('nav')?.querySelectorAll('a') ?? [])];

    expect(links.length).toBeGreaterThan(schema.blocks.length);

    for (const link of links) {
      const href = link.getAttribute('href') ?? '';

      expect(href, 'a sidebar entry that is not a fragment').toMatch(/^#/);
      expect(
        container.querySelector(`#${CSS.escape(href.slice(1))}`),
        `${href} is in the sidebar and not on the page`,
      ).not.toBeNull();
    }
  });

  it('renders every key, with its value kind and its flags', () => {
    const { container } = render(<Reference />);

    for (const block of schema.blocks)
      for (const key of block.keys) {
        const id = anchorForKey(block.kind, key.name);
        const row = container.querySelector(`#${CSS.escape(id)}`);

        expect(row, `${block.kind}.${key.name} has no row`).not.toBeNull();
        expect(row?.textContent).toContain(key.name);
        expect(row?.textContent).toContain(key.kind);
        expect(row?.textContent).toContain(key.doc.replaceAll('`', ''));

        if (key.required) expect(row?.textContent).toMatch(/required/i);
        if (key.overridable) expect(row?.textContent).toMatch(/overridable/i);
      }
  });

  it('renders every value kind and every closed member', () => {
    const { container } = render(<Reference />);

    for (const kind of schema.valueKinds) {
      const row = container.querySelector(`#${CSS.escape(anchorForValueKind(kind.name))}`);

      expect(row, `value kind ${kind.name} has no row`).not.toBeNull();

      for (const member of kind.members)
        expect(row?.textContent, `${kind.name} is missing ${member}`).toContain(member);
    }
  });

  it('renders every mode', () => {
    const { container } = render(<Reference />);

    for (const mode of schema.modes) expect(container.textContent).toContain(mode.name);
  });

  it('every value-kind link on a key row resolves to a row on the page', () => {
    // A cross-reference the browser would only report as a dead anchor, and
    // only to whoever clicked it.
    const { container } = render(<Reference />);

    for (const block of schema.blocks)
      for (const key of block.keys)
        expect(
          container.querySelector(`#${CSS.escape(anchorForValueKind(key.kind))}`),
          `${block.kind}.${key.name} links to a value kind with no row`,
        ).not.toBeNull();
  });

  it('renders nothing the schema does not declare', () => {
    // The other direction: a hand-written section for a block the schema
    // dropped is a stale page, and only this half catches it.
    const { container } = render(<Reference />);

    const known = new Set<string>([
      ...schema.blocks.map((b) => anchorForBlock(b.kind)),
      ...schema.blocks.flatMap((b) => b.keys.map((k) => anchorForKey(b.kind, k.name))),
      ...schema.valueKinds.map((k) => anchorForValueKind(k.name)),
    ]);

    for (const el of container.querySelectorAll('[data-schema-anchor]'))
      expect(known, `${el.id} is on the page and not in the schema`).toContain(el.id);
  });

  it('an anchor cannot collide with another', () => {
    // `${block}-${key}` is injective because no BLOCK KIND contains a hyphen,
    // so the first hyphen always separates the two halves and the split is
    // unambiguous. Key names very much do contain one - parallel-fifths,
    // voice-crossing, dissonance-on-strong - which is why the invariant is
    // asserted on the block half only. The first version of this test asserted
    // it on both and was wrong about the language.
    const anchors = schema.blocks.flatMap((b) => [
      anchorForBlock(b.kind),
      ...b.keys.map((k) => anchorForKey(b.kind, k.name)),
    ]);

    expect(new Set(anchors).size).toBe(anchors.length);

    for (const block of schema.blocks) expect(block.kind).not.toContain('-');
  });
});
