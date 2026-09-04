import Link from 'next/link';
import { t } from '@/lib/strings';

const links = [
  { href: '/features/', label: t('nav.features') },
  { href: '/score/', label: t('nav.score') },
  { href: '/score/reference/', label: t('nav.reference') },
  { href: '/design/', label: t('nav.design') },
] as const;

/** The app's tab strip, as a site header: stripTabs is 30px and the active tab
 *  carries an accent underline. Sticky, because the reference is one long page.
 */
export function Nav() {
  return (
    <header className="border-b-hairline border-divider bg-background/95 sticky top-0 z-10 backdrop-blur">
      <nav className="gap-xl px-xl py-lg mx-auto flex max-w-[72rem] items-center">
        <Link href="/" className="text-title text-primary font-semibold">
          {t('site.name')}
        </Link>

        <ul className="gap-lg text-body flex flex-wrap">
          {links.map((link) => (
            <li key={link.href}>
              <Link href={link.href} className="text-secondary hover:text-primary">
                {link.label}
              </Link>
            </li>
          ))}
        </ul>
      </nav>
    </header>
  );
}

export function Footer() {
  return (
    <footer className="border-t-hairline mt-xxl border-divider">
      <div className="px-xl py-xxl text-small text-secondary mx-auto max-w-[72rem]">
        <p>{t('footer.generated')}</p>
        <p className="mt-sm">{t('footer.licence')}</p>
      </div>
    </footer>
  );
}
