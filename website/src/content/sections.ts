/** The pages of a multi-page section, for the sidebar that runs across all of
 *  them. Written out rather than derived from the route tree: the order and the
 *  short labels are editorial, and a static export has no tree to walk. */
import type { TocPages } from '@/ui/Toc';
import { t } from '@/lib/strings';

const pages = (current: string, items: readonly { href: string; label: string }[]): TocPages => ({
  title: t('nav.inThisSection'),
  items: items.map((item) => ({ ...item, current: item.href === current })),
});

export const scorePages = (current: string): TocPages =>
  pages(current, [
    { href: '/score/', label: t('nav.overview') },
    { href: '/score/reference/', label: t('nav.reference') },
  ]);

export const mcpPages = (current: string): TocPages =>
  pages(current, [
    { href: '/mcp/', label: t('nav.overview') },
    { href: '/mcp/reference/', label: t('nav.reference') },
    { href: '/mcp/resources/', label: t('nav.resources') },
  ]);
