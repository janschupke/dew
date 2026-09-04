import { render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

/*  The active tab.
 *
 *  Nav.tsx's own doc comment said "the active tab carries an accent underline"
 *  from the day the file was written, and there was no active state in it at
 *  all - no usePathname, no aria-current, no underline. A comment cannot fail,
 *  which is the whole argument for this file existing.
 *
 *  The prefix rule is the part worth pinning: the reference lives under
 *  /score/, so a naive `startsWith` lights both tabs. Longest match wins.
 */
let pathname = '/';

vi.mock('next/navigation', () => ({ usePathname: () => pathname }));

const { Nav } = await import('@/ui/Nav');

const currentTab = () => {
  const marked = screen
    .getAllByRole('link')
    .filter((link) => link.getAttribute('aria-current') === 'page');

  return marked.map((link) => link.textContent);
};

afterEach(() => {
  pathname = '/';
});

describe('the nav', () => {
  it('marks the page you are on, and only that one', () => {
    pathname = '/features/';
    render(<Nav />);

    expect(currentTab()).toEqual(['Features']);
  });

  it('gives the reference its own tab rather than the score language’s', () => {
    // /score/reference/ starts with /score/ too. Longest match wins, or both
    // light up and the underline stops meaning "you are here".
    pathname = '/score/reference/';
    render(<Nav />);

    expect(currentTab()).toEqual(['Reference']);
  });

  it('marks the score language when that is where you are', () => {
    pathname = '/score/';
    render(<Nav />);

    expect(currentTab()).toEqual(['Score language']);
  });

  it('marks nothing on the home page', () => {
    // '/' is a prefix of every href, so an unguarded startsWith marks all five.
    pathname = '/';
    render(<Nav />);

    expect(currentTab()).toEqual([]);
  });

  it('is pinned to the top of the page', () => {
    // `sticky top-0` was the markup for as long as the file existed and the
    // header scrolled away regardless: --spacing: initial deletes the scale
    // `top-0` reads, so the offset emitted nothing and a sticky box with
    // `top: auto` is a static one. `pinned` is a custom utility carrying both
    // declarations, which is the only form of this that cannot half-apply.
    pathname = '/';
    const { container } = render(<Nav />);

    const header = container.querySelector('header');

    expect(header?.className).toContain('pinned');
    expect(header?.className).not.toMatch(/\btop-\d/);
  });

  it('offers the repository', () => {
    pathname = '/';
    render(<Nav />);

    expect(screen.getByRole('link', { name: 'Source' })).toHaveAttribute(
      'href',
      'https://github.com/janschupke/dew',
    );
  });
});
