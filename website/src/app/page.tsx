import Link from 'next/link';

import { Button } from '@/ui/Button';
import { Band, Card, Container } from '@/ui/Surface';
import { Lead, P } from '@/ui/Prose';
import { Shot } from '@/ui/Shot';
import { anchorForGroup, featureGroups } from '@/content/features';
import { t } from '@/lib/strings';

/*  The hero is `text-h1` and steps up to `text-hero` at the small breakpoint.
    56px is right for the width the tagline was measured at and wrong on a
    390px phone, where it takes four lines before anything else is on screen. */
export default function Home() {
  const claims = [
    { title: t('home.predictableTitle'), body: t('home.predictableBody') },
    { title: t('home.signalTitle'), body: t('home.signalBody') },
    { title: t('home.completeTitle'), body: t('home.completeBody') },
  ] as const;

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

        {/* Said plainly and early. README.md leads with it too. */}
        <P className="mt-stack">{t('site.status')}</P>

        <div className="mt-stack gap-md flex flex-wrap">
          <Button variant="primary" href="/download/">
            {t('home.readDownload')}
          </Button>
          <Button href="/features/">{t('home.readFeatures')}</Button>
          <Button variant="ghost" href="/setup/">
            {t('home.readSetup')}
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
          {t('home.workflowTitle')}
        </h2>
        <P className="mt-stack">{t('home.workflowBody')}</P>

        {/* Flat panels: there is nowhere for one of these to go, so none of
            them answers the pointer. */}
        <ul className="mt-stack gap-stack grid sm:grid-cols-3">
          {claims.map((claim) => (
            <li key={claim.title}>
              <Card className="h-full">
                <h3 className="text-h3 text-primary leading-snug font-semibold">{claim.title}</h3>
                <p className="mt-md text-prose leading-prose text-secondary">{claim.body}</p>
              </Card>
            </li>
          ))}
        </ul>
      </Band>

      <Container className="pt-section pb-section">
        <h2 className="text-h2 text-primary leading-snug font-semibold tracking-tight">
          {t('home.groupsTitle')}
        </h2>
        <P className="mt-stack">{t('home.groupsBody')}</P>

        {/* Each card goes to its own group on /features/. */}
        <ul className="mt-stack gap-stack grid sm:grid-cols-2 lg:grid-cols-3">
          {featureGroups.map((group) => (
            <li key={group.id}>
              <Card href={`/features/#${anchorForGroup(group.id)}`} className="h-full">
                <h3 className="text-h3 text-primary leading-snug font-semibold">{group.title}</h3>
                <p className="mt-md text-prose leading-prose text-secondary">{group.body}</p>
              </Card>
            </li>
          ))}
        </ul>

        <p className="mt-stack text-prose">
          <Link href="/features/" className="text-accent hover:underline">
            {t('features.title')}
          </Link>
        </p>
      </Container>

      <Band>
        <h2 className="text-h2 text-primary leading-snug font-semibold tracking-tight">
          {t('home.openTitle')}
        </h2>
        <P className="mt-stack">{t('home.openBody')}</P>

        <p className="mt-stack text-prose">
          <Link href="/terms/" className="text-accent hover:underline">
            {t('home.openLink')}
          </Link>
        </p>
      </Band>
    </>
  );
}
