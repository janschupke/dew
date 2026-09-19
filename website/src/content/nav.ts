/** Where the site can be navigated to, in one place.
 *
 *  The header and the footer used to hold the same list twice, by hand.
 */
import { repositoryUrl } from '@/content/setup';
import { t } from '@/lib/strings';

export interface NavLink {
  readonly href: string;
  readonly label: string;
  /** Drawn with the GitHub mark beside it. Only the repository itself gets one,
   *  not the files under it. */
  readonly mark?: boolean;
}

export interface NavColumn {
  readonly title: string;
  readonly links: readonly NavLink[];
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

/** The footer's link columns. Grouped rather than alphabetical: one seven-row
 *  list beside a two-row one is a shape, not a menu.
 *
 *  Home is the wordmark in the first column, not a row in any of these. */
export const footerColumns: readonly NavColumn[] = [
  {
    title: t('footer.productTitle'),
    links: [
      { href: '/features/', label: t('nav.features') },
      { href: '/download/', label: t('nav.download') },
      { href: '/setup/', label: t('nav.setup') },
    ],
  },
  {
    title: t('footer.referenceTitle'),
    links: [
      { href: '/score/', label: t('nav.score') },
      { href: '/mcp/', label: t('nav.mcp') },
      { href: '/design/', label: t('nav.design') },
    ],
  },
  {
    title: t('footer.projectTitle'),
    links: [
      { href: repositoryUrl, label: t('footer.sourceLabel'), mark: true },
      { href: `${repositoryUrl}/blob/master/README.md`, label: t('footer.readmeLabel') },
      { href: '/terms/', label: t('nav.terms') },
    ],
  },
] as const;

/** Every page of this site the footer reaches, flattened out of the columns
 *  above so the two cannot disagree. The external links are not pages and are
 *  not in it. */
export const footerLinks: readonly NavLink[] = footerColumns
  .flatMap((column) => column.links)
  .filter((link) => link.href.startsWith('/'));

/** Which tab is lit. Exact for the home page, which is a prefix of every other
 *  href; longest prefix otherwise, so a reference page lights its section. */
export function activeLink(pathname: string): string | null {
  return navLinks.reduce<string | null>((best, link) => {
    const here = link.href === '/' ? pathname === '/' : pathname.startsWith(link.href);

    return here && (best === null || link.href.length > best.length) ? link.href : best;
  }, null);
}
