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

  it('offers the repository', () => {
    pathname = '/';
    render(<Nav />);

    expect(screen.getByRole('link', { name: 'Source' })).toHaveAttribute(
      'href',
      'https://github.com/janschupke/dew',
    );
  });
});
