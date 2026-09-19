import Link from 'next/link';

import { Container } from '@/ui/Surface';
import { GitHubMark } from '@/ui/Icon';
import { footerLinks } from '@/content/nav';
import { repositoryUrl } from '@/content/setup';
import { t } from '@/lib/strings';

/*  The mark goes on the repository itself and not on the file under it. */
const project = [
  { href: repositoryUrl, label: t('footer.sourceLabel'), mark: true },
  { href: `${repositoryUrl}/blob/master/README.md`, label: t('footer.readmeLabel'), mark: false },
] as const;

const columnHeading = 'text-fine text-primary mb-md font-semibold tracking-wide uppercase';
const columnLink =
  'text-prose text-secondary hover:text-primary gap-sm inline-flex items-center ' +
  'transition-colors duration-[--motion-quick-ms]';

/** No top margin: it carries its own surface and its own rule, and a margin as
 *  well leaves a band of dead background above it. */
export function Footer() {
  return (
    <footer className="border-t-hairline border-divider bg-well">
      <Container className="py-section">
        <div className="gap-stack grid sm:grid-cols-2">
          <nav>
            <h2 className={columnHeading}>{t('footer.docsTitle')}</h2>
            <ul className="gap-sm flex flex-col">
              {footerLinks.map((link) => (
                <li key={link.href}>
                  <Link href={link.href} className={columnLink}>
                    {link.label}
                  </Link>
                </li>
              ))}
            </ul>
          </nav>

          <nav>
            <h2 className={columnHeading}>{t('footer.projectTitle')}</h2>
            <ul className="gap-sm flex flex-col">
              {project.map((link) => (
                <li key={link.href}>
                  <a href={link.href} className={columnLink}>
                    {link.mark ? <GitHubMark /> : null}
                    {link.label}
                  </a>
                </li>
              ))}
            </ul>
          </nav>
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
