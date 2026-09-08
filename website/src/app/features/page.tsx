import type { Metadata } from 'next';
import Link from 'next/link';

import { Card } from '@/ui/Surface';
import { PageHeader } from '@/ui/Prose';
import { Shot } from '@/ui/Shot';
import { Toc, type TocGroup } from '@/ui/Toc';
import {
  type Feature,
  anchorForFeature,
  anchorForGroup,
  featureGroups,
  featuresIn,
} from '@/content/features';
import { t } from '@/lib/strings';

export const metadata: Metadata = { title: `${t('features.title')} — ${t('site.name')}` };

/** One feature with a picture: the sentence and the screenshot side by side.
 *
 *  `xl` rather than `lg`, because there is a sidebar taking a column now — two
 *  halves of what is left at 1024px are 358px each, which is a thumbnail
 *  beside forty characters. */
function Illustrated({ feature }: { feature: Feature }) {
  return (
    <section
      id={anchorForFeature(feature.name)}
      className="scroll-mt-section pt-stack gap-x-gutter gap-y-stack grid items-start xl:grid-cols-2"
    >
      <div>
        <h3 className="text-h3 text-primary leading-snug font-semibold">{feature.name}</h3>
        <p className="mt-md text-prose leading-prose text-secondary max-w-[68ch]">{feature.body}</p>
        <FeatureLink feature={feature} />
      </div>

      <Shot name={feature.shot ?? 'channel-rack'} alt={`dew's ${feature.name.toLowerCase()}`} />
    </section>
  );
}

function FeatureLink({ feature }: { feature: Feature }) {
  if (feature.href === undefined) return null;

  return (
    <p className="mt-md text-prose">
      <Link href={feature.href} className="text-accent hover:underline">
        {feature.hrefLabel ?? feature.name}
      </Link>
    </p>
  );
}

export default function Features() {
  const toc: readonly TocGroup[] = [
    {
      id: 'groups',
      title: t('features.onThisPage'),
      items: featureGroups.map((group) => ({
        href: `#${anchorForGroup(group.id)}`,
        label: group.title,
      })),
    },
  ];

  return (
    <>
      <PageHeader title={t('features.title')} lead={t('features.lead')} />

      <Toc label={t('features.onThisPage')} groups={toc}>
        {featureGroups.map((group) => {
          const shown = featuresIn(group.id).filter((feature) => feature.shot !== undefined);
          const rest = featuresIn(group.id).filter((feature) => feature.shot === undefined);

          return (
            <section
              key={group.id}
              id={anchorForGroup(group.id)}
              data-feature-group={group.id}
              className="scroll-mt-section pt-section"
            >
              <h2 className="text-h2 text-primary leading-snug font-semibold tracking-tight">
                {group.title}
              </h2>
              <p className="mt-sm text-prose leading-prose text-secondary max-w-[68ch]">
                {group.body}
              </p>

              {shown.map((feature) => (
                <Illustrated key={feature.name} feature={feature} />
              ))}

              {rest.length > 0 ? (
                <ul className="mt-stack gap-stack grid md:grid-cols-2">
                  {rest.map((feature) => (
                    <li
                      key={feature.name}
                      id={anchorForFeature(feature.name)}
                      className="scroll-mt-section"
                    >
                      <Card className="h-full">
                        <h3 className="text-h3 text-primary leading-snug font-semibold">
                          {feature.name}
                        </h3>
                        <p className="mt-md text-prose leading-prose text-secondary">
                          {feature.body}
                        </p>
                        <FeatureLink feature={feature} />
                      </Card>
                    </li>
                  ))}
                </ul>
              ) : null}
            </section>
          );
        })}
      </Toc>
    </>
  );
}
