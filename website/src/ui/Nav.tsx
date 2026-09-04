'use client';

import Link from 'next/link';
import { usePathname } from 'next/navigation';

import { Container } from '@/ui/Surface';
import { GitHubMark } from '@/ui/Icon';
import { repositoryUrl } from '@/content/setup';
import { t } from '@/lib/strings';

const links = [
  { href: '/download/', label: t('nav.download') },
  { href: '/features/', label: t('nav.features') },
  { href: '/score/', label: t('nav.score') },
  { href: '/score/reference/', label: t('nav.reference') },
  { href: '/mcp/', label: t('nav.mcp') },
  { href: '/design/', label: t('nav.design') },
  { href: '/setup/', label: t('nav.setup') },
] as const;

/** The app's tab strip, as a site header: stripTabs is 30px and the active tab
 *  carries an accent underline. Pinned, because the reference is one long page.
 *
 *  `pinned` rather than `sticky top-0`: the second class emitted no CSS, so the
 *  header had a position and no offset and scrolled away like a static one. See
 *  theme.site.css for why, and gates.test.ts for what refuses it now.
 *
 *  A client component only for `usePathname`. That underline was described in
 *  this comment for as long as the file existed and was never drawn - there was
 *  no active state at all - which is the failure mode a comment has and a test
 *  does not, so `nav.test.tsx` holds it now.
 *
 *  The active test is a prefix rather than equality, deliberately: the
 *  reference lives under /score/, and a reader who is on it is still in the
 *  score language section. Longest match wins, so /score/reference/ lights its
 *  own tab rather than both.
 */
export function Nav() {
  const pathname = usePathname();

  const active = links.reduce<string | null>(
    (best, link) =>
      pathname.startsWith(link.href) && (best === null || link.href.length > best.length)
        ? link.href
        : best,
    null,
  );

  return (
    <header className="border-b-hairline border-divider bg-background/90 pinned z-10 backdrop-blur">
      <Container>
        <nav className="gap-x-stack gap-y-md py-lg flex flex-wrap items-baseline">
          <Link href="/" className="text-h3 text-primary font-semibold tracking-tight">
            {t('site.name')}
          </Link>

          <ul className="gap-x-xxl gap-y-md text-prose flex flex-wrap items-baseline">
            {links.map((link) => {
              const isActive = link.href === active;

              return (
                <li key={link.href}>
                  <Link
                    href={link.href}
                    aria-current={isActive ? 'page' : undefined}
                    className={
                      'border-b-regular pb-xs inline-block whitespace-nowrap ' +
                      'transition-colors duration-[--motion-quick-ms] ' +
                      (isActive
                        ? 'text-primary border-accent'
                        : 'text-secondary hover:text-primary border-transparent')
                    }
                  >
                    {link.label}
                  </Link>
                </li>
              );
            })}
          </ul>

          <a
            href={repositoryUrl}
            className="text-secondary hover:text-primary text-prose gap-sm ml-auto inline-flex items-center whitespace-nowrap transition-colors duration-[--motion-quick-ms]"
          >
            <GitHubMark />
            {t('nav.source')}
          </a>
        </nav>
      </Container>
    </header>
  );
}
