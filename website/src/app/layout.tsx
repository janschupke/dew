import type { Metadata } from 'next';
import type { ReactNode } from 'react';

import { Footer, Nav } from '@/ui/Nav';
import { t } from '@/lib/strings';
import './globals.css';

export const metadata: Metadata = {
  title: `${t('site.name')} — ${t('site.tagline')}`,
  description: t('home.intro'),
};

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

        <main id="content" className="px-xl mx-auto max-w-[72rem]">
          {children}
        </main>

        <Footer />
      </body>
    </html>
  );
}
