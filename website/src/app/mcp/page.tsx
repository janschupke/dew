import type { Metadata } from 'next';
import Link from 'next/link';

import { Band, Card, Container } from '@/ui/Surface';
import { P, PageHeader, Section } from '@/ui/Prose';
import { mcp } from '@/lib/mcp';
import { t } from '@/lib/strings';

export const metadata: Metadata = { title: `${t('mcp.title')} — ${t('site.name')}` };

/*  What the endpoint is and how to reach it. The tool-by-tool detail is the
    reference next door, which is generated; this page is the argument for it,
    and is written by hand for the reason features.ts is - a claim about what
    something is FOR cannot be derived from the table that implements it.
*/
export default function Mcp() {
  const sections = [
    { title: t('mcp.howTitle'), body: t('mcp.howBody') },
    { title: t('mcp.connectTitle'), body: t('mcp.connectBody') },
    { title: t('mcp.surfaceTitle'), body: t('mcp.surfaceBody') },
    { title: t('mcp.scoreTitle'), body: t('mcp.scoreBody') },
  ] as const;

  return (
    <>
      <PageHeader title={t('mcp.title')} lead={t('mcp.lead')} />

      <Band>
        <div className="gap-stack grid sm:grid-cols-2">
          {sections.map((section) => (
            <Card key={section.title}>
              <h2 className="text-h3 text-primary font-semibold tracking-tight">{section.title}</h2>
              <P className="mt-sm">{section.body}</P>
            </Card>
          ))}
        </div>
      </Band>

      <Container>
        <Section title={t('mcpReference.guideTitle')}>
          <P>{t('mcpReference.guideLead')}</P>

          <ul className="mt-stack gap-sm flex flex-col">
            {mcp.guide.map((section) => (
              <li key={section.id} className="text-prose">
                <Link
                  href={`/mcp/reference/#guide-${section.id}`}
                  className="text-accent hover:underline"
                >
                  {section.title}
                </Link>
                <span className="text-secondary"> — {section.summary}</span>
              </li>
            ))}
          </ul>

          <P className="mt-stack">
            <Link href="/mcp/reference/" className="text-accent hover:underline">
              {t('mcp.referenceLink')}
            </Link>
          </P>
        </Section>
      </Container>
    </>
  );
}
