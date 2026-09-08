import type { Metadata } from 'next';

import { Container } from '@/ui/Surface';
import { P, PageHeader, Section } from '@/ui/Prose';
import { licenceUrl, terms } from '@/content/terms';
import { t } from '@/lib/strings';

export const metadata: Metadata = { title: `${t('terms.title')} — ${t('site.name')}` };

export default function Terms() {
  return (
    <>
      <PageHeader title={t('terms.title')} lead={t('terms.lead')} />

      <Container className="pb-section">
        {terms.map((section) => (
          <Section key={section.id} id={section.id} title={section.title}>
            {section.paragraphs.map((paragraph, i) => (
              <P key={i} className={i > 0 ? 'mt-stack' : ''}>
                {paragraph}
              </P>
            ))}
          </Section>
        ))}

        <p className="mt-section text-prose">
          <a href={licenceUrl} className="text-accent hover:underline">
            {t('terms.sourceCta')}
          </a>
        </p>
      </Container>
    </>
  );
}
