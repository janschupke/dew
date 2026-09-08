/** Where the site can be navigated to, in one place.
 *
 *  The header and the footer used to hold the same list twice, by hand.
 */
import { t } from '@/lib/strings';

export interface NavLink {
  readonly href: string;
  readonly label: string;
}

/** The header's tabs, in order. */
export const navLinks: readonly NavLink[] = [
  { href: '/', label: t('nav.home') },
  { href: '/features/', label: t('nav.features') },
  { href: '/score/', label: t('nav.score') },
  { href: '/mcp/', label: t('nav.mcp') },
  { href: '/download/', label: t('nav.download') },
  { href: '/setup/', label: t('nav.setup') },
] as const;

/** The footer's first column: the header's tabs, plus the two pages that are
 *  reachable but not worth a tab. Home is the wordmark, not a row. */
export const footerLinks: readonly NavLink[] = [
  ...navLinks.filter((link) => link.href !== '/'),
  { href: '/design/', label: t('nav.design') },
  { href: '/terms/', label: t('nav.terms') },
] as const;

/** Which tab is lit. Exact for the home page, which is a prefix of every other
 *  href; longest prefix otherwise, so a reference page lights its section. */
export function activeLink(pathname: string): string | null {
  return navLinks.reduce<string | null>((best, link) => {
    const here = link.href === '/' ? pathname === '/' : pathname.startsWith(link.href);

    return here && (best === null || link.href.length > best.length) ? link.href : best;
  }, null);
}
