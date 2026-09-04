import type { Metadata } from 'next';

import { Card } from '@/ui/Surface';
import { Lead } from '@/ui/Prose';
import { Shot } from '@/ui/Shot';
import { features } from '@/content/features';
import { t } from '@/lib/strings';

export const metadata: Metadata = { title: `${t('features.title')} — ${t('site.name')}` };

export default function Features() {
  return (
    <section className="py-xxl">
      <h1 className="text-h1 text-primary font-semibold">{t('features.title')}</h1>
      <Lead>{t('features.lead')}</Lead>

      {/* The five with a shot lead, because a picture of a DAW says more than a
          paragraph about one. The rest follow as cards. */}
      {features
        .filter((feature) => feature.shot)
        .map((feature) => (
          <section key={feature.name} className="py-xl">
            <h2 className="text-h2 text-primary font-semibold">{feature.name}</h2>
            <p className="mt-sm text-prose text-secondary max-w-[68ch]">{feature.body}</p>
            <Shot name={feature.shot ?? ''} alt={`dew's ${feature.name.toLowerCase()}`} />
          </section>
        ))}

      <ul className="mt-xxl gap-lg grid md:grid-cols-2">
        {features
          .filter((feature) => !feature.shot)
          .map((feature) => (
            <li key={feature.name}>
              <Card className="h-full">
                <h2 className="text-title text-primary font-semibold">{feature.name}</h2>
                <p className="mt-sm text-body text-secondary">{feature.body}</p>
              </Card>
            </li>
          ))}
      </ul>
    </section>
  );
}
