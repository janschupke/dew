import type { Metadata } from 'next';

import { Band, Card, Container } from '@/ui/Surface';
import { PageHeader } from '@/ui/Prose';
import { Shot } from '@/ui/Shot';
import { features } from '@/content/features';
import { t } from '@/lib/strings';

export const metadata: Metadata = { title: `${t('features.title')} — ${t('site.name')}` };

export default function Features() {
  const shown = features.filter((feature) => feature.shot !== undefined);
  const rest = features.filter((feature) => feature.shot === undefined);

  return (
    <>
      <PageHeader title={t('features.title')} lead={t('features.lead')} />

      {/* The five with a shot lead, because a picture of a DAW says more than a
          paragraph about one. The rest follow as cards, in a band, so where the
          page changes from one shape to the other is visible rather than
          inferred from a heading. */}
      <Container className="pb-section">
        {shown.map((feature) => (
          <section key={feature.name} className="py-section">
            <h2 className="text-h2 text-primary leading-snug font-semibold tracking-tight">
              {feature.name}
            </h2>
            <p className="mt-stack text-prose leading-prose text-secondary max-w-[68ch]">
              {feature.body}
            </p>
            <Shot
              name={feature.shot ?? 'channel-rack'}
              alt={`dew's ${feature.name.toLowerCase()}`}
            />
          </section>
        ))}
      </Container>

      <Band>
        <ul className="gap-stack grid md:grid-cols-2">
          {rest.map((feature) => (
            <li key={feature.name}>
              <Card className="h-full">
                <h2 className="text-h3 text-primary leading-snug font-semibold">{feature.name}</h2>
                <p className="mt-md text-prose leading-prose text-secondary">{feature.body}</p>
              </Card>
            </li>
          ))}
        </ul>
      </Band>
    </>
  );
}
