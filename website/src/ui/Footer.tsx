import Link from 'next/link';

import { Container } from '@/ui/Surface';
import { GitHubMark } from '@/ui/Icon';
import { type NavLink, footerColumns } from '@/content/nav';
import { t } from '@/lib/strings';

const columnHeading = 'text-fine text-primary mb-md font-semibold tracking-wide uppercase';
const columnLink =
  'text-prose text-secondary hover:text-primary gap-sm inline-flex items-center ' +
  'transition-colors duration-[--motion-quick-ms]';

/** A row in one of the columns. An internal href is a `next/link`, an external
 *  one a plain anchor: `next/link` prefetches, and there is nothing on GitHub
 *  for it to prefetch. */
function Row({ link }: { link: NavLink }) {
  if (link.href.startsWith('/'))
    return (
      <Link href={link.href} className={columnLink}>
        {link.label}
      </Link>
    );

  return (
    <a href={link.href} className={columnLink}>
      {link.mark === true ? <GitHubMark /> : null}
      {link.label}
    </a>
  );
}

/** No top margin: it carries its own surface and its own rule, and a margin as
 *  well leaves a band of dead background above it.
 *
 *  Four columns, not two. The first is the wordmark and what dew currently is;
 *  the other three come from `footerColumns`, which the flattened `footerLinks`
 *  the tests read is derived from. */
export function Footer() {
  return (
    <footer className="border-t-hairline border-divider bg-well">
      <Container className="py-section">
        <div className="gap-stack grid sm:grid-cols-2 lg:grid-cols-4">
          <div>
            <p className="text-h3 text-primary leading-snug font-semibold tracking-tight">
              {t('site.name')}
            </p>
            <p className="mt-md text-fine text-secondary leading-prose">{t('site.status')}</p>
          </div>

          {footerColumns.map((column) => (
            <nav key={column.title}>
              <h2 className={columnHeading}>{column.title}</h2>
              <ul className="gap-sm flex flex-col">
                {column.links.map((link) => (
                  <li key={link.href}>
                    <Row link={link} />
                  </li>
                ))}
              </ul>
            </nav>
          ))}
        </div>

        <div className="border-t-hairline border-divider mt-stack pt-stack text-fine text-secondary gap-x-md flex flex-wrap">
          <p>{t('footer.licence')}</p>
          <Link href="/terms/" className="text-accent hover:underline">
            {t('footer.licenceLink')}
          </Link>
        </div>
      </Container>
    </footer>
  );
}
