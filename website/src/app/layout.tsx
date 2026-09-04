import type { Metadata } from 'next';
import type { ReactNode } from 'react';

import { Footer } from '@/ui/Footer';
import { Nav } from '@/ui/Nav';
import { t } from '@/lib/strings';
import './globals.css';

/*  Where a relative image URL resolves from, for the open-graph tag.
 *
 *  Vercel sets VERCEL_PROJECT_PRODUCTION_URL at build time and there is no
 *  domain committed anywhere in this repository, so the deployment names
 *  itself. Locally there is nothing to resolve against and localhost is
 *  honest about that - the alternative is inventing a hostname here, which
 *  would be a URL the tree could not check.
 */
const siteUrl = process.env.VERCEL_PROJECT_PRODUCTION_URL
  ? `https://${process.env.VERCEL_PROJECT_PRODUCTION_URL}`
  : 'http://localhost:3000';

export const metadata: Metadata = {
  metadataBase: new URL(siteUrl),
  title: `${t('site.name')} — ${t('site.tagline')}`,
  description: t('home.intro'),
  openGraph: {
    title: `${t('site.name')} — ${t('site.tagline')}`,
    description: t('home.intro'),
    images: ['/shots/channel-rack.png'],
    type: 'website',
  },
};

/** `<main>` carries no width of its own.
 *
 *  A Band has to paint edge to edge, and a max-width on the element every page
 *  sits inside makes that impossible without a negative margin measured against
 *  the very number it is escaping. So the column is a Container, applied by
 *  whatever needs one, and the page decides where a band interrupts it.
 */
export default function RootLayout({ children }: { children: ReactNode }) {
  return (
    <html lang="en">
      <body>
        <a
          href="#content"
          className="focus:m-md focus:bg-surface focus:p-md sr-only focus:not-sr-only focus:absolute"
        >
          {t('site.skipToContent')}
        </a>

        <Nav />

        <main id="content">{children}</main>

        <Footer />
      </body>
    </html>
  );
}
