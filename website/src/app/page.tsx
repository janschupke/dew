import Link from 'next/link';

import { Button } from '@/ui/Button';
import { Card } from '@/ui/Surface';
import { Lead } from '@/ui/Prose';
import { features } from '@/content/features';
import { t } from '@/lib/strings';

export default function Home() {
  return (
    <>
      <section className="py-xxl">
        <h1 className="text-hero text-primary font-semibold tracking-tight">{t('site.name')}</h1>

        <p className="mt-lg text-h2 text-primary max-w-[68ch]">{t('site.tagline')}</p>

        <Lead>{t('home.intro')}</Lead>

        {/* Said plainly and early. README.md leads with it too, and a product
            page that buried it would be the one place in the tree that did. */}
        <p className="mt-lg text-body text-secondary">{t('site.status')}</p>

        <div className="mt-xl gap-md flex flex-wrap">
          <Button variant="primary" href="/features/">
            {t('home.readFeatures')}
          </Button>
          <Button href="/score/">{t('home.readScore')}</Button>
        </div>
      </section>

      <section className="py-xxl">
        <h2 className="text-h2 text-primary font-semibold">{t('home.tabsTitle')}</h2>
        <p className="mt-md text-prose text-secondary max-w-[68ch]">{t('home.tabsBody')}</p>

        <ul className="mt-xl gap-lg grid sm:grid-cols-2">
          {features.slice(0, 5).map((feature) => (
            <li key={feature.name}>
              <Card>
                <h3 className="text-title text-primary font-semibold">{feature.name}</h3>
                <p className="mt-sm text-body text-secondary">{feature.body}</p>
              </Card>
            </li>
          ))}
        </ul>

        <p className="mt-xl text-body">
          <Link href="/features/" className="text-accent hover:underline">
            {t('features.title')}
          </Link>
        </p>
      </section>
    </>
  );
}
