import type { Metadata } from 'next';

import { Card } from '@/ui/Surface';
import { Lead } from '@/ui/Prose';
import { features } from '@/content/features';
import { t } from '@/lib/strings';

export const metadata: Metadata = { title: `${t('features.title')} — ${t('site.name')}` };

export default function Features() {
  return (
    <section className="py-xxl">
      <h1 className="text-h1 text-primary font-semibold">{t('features.title')}</h1>
      <Lead>{t('features.lead')}</Lead>

      <ul className="mt-xxl gap-lg grid md:grid-cols-2">
        {features.map((feature) => (
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
