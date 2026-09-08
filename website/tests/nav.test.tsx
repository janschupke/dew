import { render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

/*  The active tab. Nav.tsx's doc comment claimed an accent underline for as
    long as the file existed and there was no active state in it at all, which
    is the failure mode a comment has and a test does not.
*/
let pathname = '/';

vi.mock('next/navigation', () => ({ usePathname: () => pathname }));

const { Nav } = await import('@/ui/Nav');
const { Footer } = await import('@/ui/Footer');
const { navLinks, footerLinks } = await import('@/content/nav');

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
  it('offers Home explicitly, and marks it on the home page', () => {
    // '/' is a prefix of every href, so a plain startsWith marks every tab.
    // Home is matched exactly and everything else by prefix.
    pathname = '/';
    render(<Nav />);

    expect(currentTab()).toEqual(['Home']);
  });

  it('marks the page you are on, and only that one', () => {
    pathname = '/features/';
    render(<Nav />);

    expect(currentTab()).toEqual(['Features']);
  });

  it('lights the section, not a tab of its own, on a reference page', () => {
    // The score reference and the MCP reference are pages INSIDE a section, and
    // the header says which section you are in. Their own sidebar says which
    // page of it.
    pathname = '/score/reference/';
    render(<Nav />);

    expect(currentTab()).toEqual(['Score']);
  });

  it('lights MCP from all three of its pages', () => {
    for (const path of ['/mcp/', '/mcp/reference/', '/mcp/resources/']) {
      pathname = path;
      const view = render(<Nav />);

      expect(currentTab(), path).toEqual(['MCP']);
      view.unmount();
    }
  });

  it('is pinned to the top of the page', () => {
    // `sticky top-0` was the markup for as long as the file existed and the
    // header scrolled away regardless: --spacing: initial deletes the scale
    // `top-0` reads, so the offset emitted nothing.
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

describe('the footer', () => {
  it('carries every tab the header does', () => {
    // The two lists were written out by hand, twice, and are one list now.
    // This is what stops them being two again.
    const { container } = render(<Footer />);

    // Trailing slashes stripped on both sides: next/link normalises `/a/` to
    // `/a`, and `trailingSlash: true` is a next.config setting nothing here
    // applies - so asserting the string as written asserts Next's formatting.
    const path = (href: string) => href.replace(/\/$/, '');

    const hrefs = new Set(
      [...container.querySelectorAll('a')].map((a) => path(a.getAttribute('href') ?? '')),
    );

    for (const link of [...navLinks, ...footerLinks])
      if (link.href !== '/') expect(hrefs, link.href).toContain(path(link.href));
  });

  it('reaches the pages the header has no room for', () => {
    const hrefs = footerLinks.map((link) => link.href);

    expect(hrefs).toContain('/design/');
    expect(hrefs).toContain('/terms/');
    expect(hrefs).not.toContain('/');
  });

  it('does not send a reader into the repository rules', () => {
    // `.ai/rules/` is where the project argues with itself. It is not a page on
    // a product site, and it was linked from every one of them.
    const { container } = render(<Footer />);

    expect(container.innerHTML).not.toContain('.ai/rules');
  });
});
