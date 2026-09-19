import type { Metadata } from 'next';
import type { ReactNode } from 'react';

import { Footer } from '@/ui/Footer';
import { Nav } from '@/ui/Nav';
import { RouteFade } from '@/ui/Transition';
import { t } from '@/lib/strings';
import './globals.css';

/*  Where a relative image URL resolves from, for the open-graph tag. No domain
 *  is committed anywhere in this repository, so the deployment names itself. */
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

/** `<main>` carries no width of its own: a Band has to paint edge to edge, so
 *  the column is a Container applied by whatever needs one. */
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

        <main id="content">
          <RouteFade>{children}</RouteFade>
        </main>

        <Footer />
      </body>
    </html>
  );
}
