import Link from 'next/link';

import { Container } from '@/ui/Surface';
import { GitHubMark } from '@/ui/Icon';
import { repositoryUrl } from '@/content/setup';
import { t } from '@/lib/strings';

const docs = [
  { href: '/features/', label: t('nav.features') },
  { href: '/score/', label: t('nav.score') },
  { href: '/score/reference/', label: t('nav.reference') },
  { href: '/design/', label: t('nav.design') },
  { href: '/setup/', label: t('nav.setup') },
] as const;

/*  The mark goes on the repository itself and not on the two links under it.
    Those point at a file and a directory inside it, not at the project. Three
    identical marks in a column of three would be a texture, not a signal.
*/
const project = [
  { href: repositoryUrl, label: t('footer.sourceLabel'), mark: true },
  { href: `${repositoryUrl}/blob/master/README.md`, label: t('footer.readmeLabel'), mark: false },
  { href: `${repositoryUrl}/tree/master/.ai/rules`, label: t('footer.rulesLabel'), mark: false },
] as const;

const columnHeading = 'text-fine text-primary mb-md font-semibold tracking-wide uppercase';
const columnLink =
  'text-prose text-secondary hover:text-primary gap-sm inline-flex items-center ' +
  'transition-colors duration-[--motion-quick-ms]';

/** Two columns of links and the two sentences that were the whole of it.
 *
 *  No top margin. It carries its own surface and its own rule, and a margin as
 *  well left a band of dead background between the last section's bottom edge
 *  and the footer's top one - two horizontal rules with nothing between them,
 *  which reads as a mistake rather than as space.
 *
 *  It used to be those sentences alone, set at the app's 12px `small` rung -
 *  the end of the page saying nothing and offering nowhere to go next, which on
 *  a site of six pages is most of the navigation missing.
 */
export function Footer() {
  return (
    <footer className="border-t-hairline border-divider bg-well">
      <Container className="py-section">
        <div className="gap-stack grid sm:grid-cols-2">
          <nav>
            <h2 className={columnHeading}>{t('footer.docsTitle')}</h2>
            <ul className="gap-sm flex flex-col">
              {docs.map((link) => (
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

        <div className="border-t-hairline border-divider mt-stack pt-stack text-fine text-secondary">
          <p>{t('footer.generated')}</p>
          <p className="mt-sm">{t('footer.licence')}</p>
        </div>
      </Container>
    </footer>
  );
}
