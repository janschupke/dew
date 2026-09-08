'use client';

import Link from 'next/link';
import { usePathname } from 'next/navigation';

import { Container } from '@/ui/Surface';
import { GitHubMark } from '@/ui/Icon';
import { activeLink, navLinks } from '@/content/nav';
import { repositoryUrl } from '@/content/setup';
import { t } from '@/lib/strings';

/** The app's tab strip, as a site header. The active tab carries an accent
 *  underline; `nav.test.tsx` holds which one that is.
 *
 *  `pinned` rather than `sticky top-0`: the second class emits no CSS, because
 *  `--spacing: initial` deletes the scale a numeric offset reads. See
 *  theme.site.css.
 *
 *  A client component only for `usePathname`.
 */
export function Nav() {
  const active = activeLink(usePathname());

  return (
    <header className="border-b-hairline border-divider bg-background/90 pinned z-10 backdrop-blur">
      <Container>
        <nav className="gap-x-stack gap-y-md py-lg flex flex-wrap items-baseline">
          <Link href="/" className="text-h3 text-primary font-semibold tracking-tight">
            {t('site.name')}
          </Link>

          <ul className="gap-x-xxl gap-y-md text-prose flex flex-wrap items-baseline">
            {navLinks.map((link) => {
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
