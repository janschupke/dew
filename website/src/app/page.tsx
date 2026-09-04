import Link from 'next/link';

import { Button } from '@/ui/Button';
import { Band, Card, Container } from '@/ui/Surface';
import { Lead, P } from '@/ui/Prose';
import { Shot } from '@/ui/Shot';
import { anchorForFeature, features } from '@/content/features';
import { t } from '@/lib/strings';

/*  The hero is `text-h1` and steps up to `text-hero` at the small breakpoint.
    56px is right for the width the tagline was measured at and wrong on a
    390px phone, where it takes four lines before anything else is on screen.
    Both are rungs on the site's ladder rather than a value written here.
*/
export default function Home() {
  return (
    <>
      <Container className="pt-band pb-section">
        <h1 className="text-h1 sm:text-hero text-primary leading-tight font-semibold tracking-tight">
          {t('site.name')}
        </h1>

        <p className="mt-stack text-h2 text-primary max-w-[68ch] leading-snug tracking-tight">
          {t('site.tagline')}
        </p>

        <Lead>{t('home.intro')}</Lead>

        {/* Said plainly and early. README.md leads with it too, and a product
            page that buried it would be the one place in the tree that did. */}
        <P className="mt-stack">{t('site.status')}</P>

        <div className="mt-stack gap-md flex flex-wrap">
          <Button variant="primary" href="/setup/">
            {t('home.readSetup')}
          </Button>
          <Button href="/features/">{t('home.readFeatures')}</Button>
          <Button variant="ghost" href="/score/">
            {t('home.readScore')}
          </Button>
        </div>
      </Container>

      <Container className="pb-section">
        <Shot
          name="channel-rack"
          alt="dew's channel rack, a step grid with one row per channel"
          priority
        />
      </Container>

      <Band>
        <h2 className="text-h2 text-primary leading-snug font-semibold tracking-tight">
          {t('home.tabsTitle')}
        </h2>
        <P className="mt-stack">{t('home.tabsBody')}</P>

        {/* Each card goes to its own section on /features/. They were divs that
            lit up under the pointer and did nothing when clicked, which is the
            one thing a card must not be. */}
        <ul className="mt-stack gap-stack grid sm:grid-cols-2 lg:grid-cols-3">
          {features.slice(0, 6).map((feature) => (
            <li key={feature.name}>
              <Card href={`/features/#${anchorForFeature(feature.name)}`} className="h-full">
                <h3 className="text-h3 text-primary leading-snug font-semibold">{feature.name}</h3>
                <p className="mt-md text-prose leading-prose text-secondary">{feature.body}</p>
              </Card>
            </li>
          ))}
        </ul>

        <p className="mt-stack text-prose">
          <Link href="/features/" className="text-accent hover:underline">
            {t('features.title')}
          </Link>
        </p>
      </Band>
    </>
  );
}
