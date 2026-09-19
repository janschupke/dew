import { render, screen, waitFor } from '@testing-library/react';
import { describe, expect, it } from 'vitest';

import { Toc, type TocGroup } from '@/ui/Toc';

/*  The sidebar marking what is on screen.
 *
 *  jsdom has no layout, so every getBoundingClientRect is zeros - which is why
 *  each case stubs the rects it is about. The stub IS the fixture here: what is
 *  under test is which entry the rule picks given a set of positions, not
 *  whether jsdom can lay a page out.
 */
/** `s0`, `s1`, … as strings, so no number reaches a template literal. */
const ids = (n: number): string[] => Array.from({ length: n }, (_, i) => `s${String(i)}`);

const groups = (n: number): readonly TocGroup[] => [
  {
    id: 'sections',
    title: 'Sections',
    items: ids(n).map((id, i) => ({ href: `#${id}`, label: `Section ${String(i)}` })),
  },
];

function Harness({ count }: { count: number }) {
  return (
    <Toc
      label="On this page"
      pages={{
        title: 'In this section',
        items: [
          { href: '/here/', label: 'Overview', current: true },
          { href: '/there/', label: 'Reference' },
        ],
      }}
      groups={groups(count)}
    >
      {ids(count).map((id, i) => (
        <section key={id} id={id}>
          Section {i}
        </section>
      ))}
    </Toc>
  );
}

/** Put each section where the test says it is, then scroll. */
function place(tops: readonly number[]) {
  ids(tops.length).forEach((id, i) => {
    const element = document.getElementById(id);

    if (element === null) throw new Error(`no #${id} to place`);

    const top = tops[i] ?? 0;

    element.getBoundingClientRect = () => ({ top }) as DOMRect;
  });

  window.dispatchEvent(new Event('scroll'));
}

const marked = () =>
  [...document.querySelectorAll('[data-toc-group="sections"] a')]
    .filter((link) => link.getAttribute('aria-current') === 'true')
    .map((link) => link.textContent);

describe('the sidebar', () => {
  it('marks the last section whose top has passed the header', () => {
    // Two are above the line and one is below it, so the second is what a
    // reader is looking at - a section stays current until the next one
    // reaches the same line.
    render(<Harness count={3} />);
    place([-500, -100, 300]);

    return waitFor(() => {
      expect(marked()).toEqual(['Section 1']);
    });
  });

  it('falls back to the first when nothing has passed it', () => {
    // The top of the page, where no section has reached the header yet and
    // an unguarded "last match" would mark nothing at all.
    render(<Harness count={3} />);
    place([100, 200, 300]);

    return waitFor(() => {
      expect(marked()).toEqual(['Section 0']);
    });
  });

  it('follows the reader down the page', () => {
    render(<Harness count={3} />);
    place([-900, -600, -200]);

    return waitFor(() => {
      expect(marked()).toEqual(['Section 2']);
    });
  });

  it('marks exactly one entry', () => {
    // Two marked entries is the same as none.
    render(<Harness count={4} />);
    place([-500, -400, -300, 200]);

    return waitFor(() => {
      expect(marked()).toHaveLength(1);
    });
  });

  it('marks the page of the section you are on', () => {
    // The pages group is how a reader gets from a reference back to the page
    // that explains it, and it has to say which of them this is.
    render(<Harness count={2} />);

    const links = [...document.querySelectorAll('[data-toc-group="pages"] a')];

    expect(links.map((link) => link.textContent)).toEqual(['Overview', 'Reference']);
    expect(links.filter((link) => link.getAttribute('aria-current') === 'page')).toHaveLength(1);
    expect(links[0]?.getAttribute('aria-current')).toBe('page');
  });

  it('is a landmark with a name', () => {
    render(<Harness count={2} />);

    expect(screen.getByRole('navigation', { name: 'On this page' })).toBeInTheDocument();
  });
});
