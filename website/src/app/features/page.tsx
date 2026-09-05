import type { Metadata } from 'next';

import { Band, Card, Container } from '@/ui/Surface';
import { PageHeader } from '@/ui/Prose';
import { Shot } from '@/ui/Shot';
import { anchorForFeature, features } from '@/content/features';
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
          inferred from a heading.

          TWO COLUMNS from `lg`: the sentence and the picture it describes are
          side by side rather than the sentence sitting on top of an image tall
          enough to push the next one off the screen. Below `lg` they stack in
          the same order, which is the order they were written in.

          The shot is therefore rendered at about half the page, which is why it
          opens full-screen on a click - a 2880px render shown at 500px is a
          thumbnail, and a thumbnail nobody can enlarge is a picture of a
          picture.

          Both groups carry an anchor. The home page shows the first six of the
          same list, which straddles the split - `Instrument` has no shot and is
          a card down there - so an id on the shot sections alone would break
          one of its six links and only one. */}
      <Container className="pb-section">
        {shown.map((feature) => (
          <section
            key={feature.name}
            id={anchorForFeature(feature.name)}
            className="scroll-mt-section pt-section gap-x-gutter gap-y-stack grid items-start lg:grid-cols-2"
          >
            <div>
              <h2 className="text-h2 text-primary leading-snug font-semibold tracking-tight">
                {feature.name}
              </h2>
              <p className="mt-stack text-prose leading-prose text-secondary max-w-[68ch]">
                {feature.body}
              </p>
            </div>

            <Shot
              name={feature.shot ?? 'channel-rack'}
              alt={`dew's ${feature.name.toLowerCase()}`}
              className=""
            />
          </section>
        ))}
      </Container>

      <Band>
        <ul className="gap-stack grid md:grid-cols-2">
          {rest.map((feature) => (
            <li
              key={feature.name}
              id={anchorForFeature(feature.name)}
              className="scroll-mt-section"
            >
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
