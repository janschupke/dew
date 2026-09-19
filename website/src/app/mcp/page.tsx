import type { Metadata } from 'next';
import Link from 'next/link';

import { P, PageHeader, Section } from '@/ui/Prose';
import { Toc, type TocGroup } from '@/ui/Toc';
import { anchorForGuide, mcp } from '@/lib/mcp';
import { mcpPages } from '@/content/sections';
import { t } from '@/lib/strings';

export const metadata: Metadata = { title: `${t('mcp.title')} — ${t('site.name')}` };

/*  What the endpoint is and how to reach it. The tool-by-tool detail is the
    reference next door, which is generated; this page is written by hand. */
const sections = [
  { id: 'how-it-works', title: t('mcp.howTitle'), body: t('mcp.howBody') },
  { id: 'connecting', title: t('mcp.connectTitle'), body: t('mcp.connectBody') },
  { id: 'what-it-reaches', title: t('mcp.surfaceTitle'), body: t('mcp.surfaceBody') },
  { id: 'writing-a-score', title: t('mcp.scoreTitle'), body: t('mcp.scoreBody') },
] as const;

export default function Mcp() {
  const toc: readonly TocGroup[] = [
    {
      id: 'mcp',
      title: t('mcp.onThisPage'),
      items: [
        ...sections.map((section) => ({ href: `#${section.id}`, label: section.title })),
        { href: '#resources', label: t('mcpResources.title') },
      ],
    },
  ];

  return (
    <>
      <PageHeader title={t('mcp.title')} lead={t('mcp.lead')} />

      <Toc label={t('mcp.onThisPage')} pages={mcpPages('/mcp/')} groups={toc}>
        {sections.map((section) => (
          <Section key={section.id} id={section.id} title={section.title}>
            <P>{section.body}</P>
          </Section>
        ))}

        {/* The anchor comes from anchorForGuide rather than from `guide-` typed
            out here: the resources live on a page of their own. */}
        <Section id="resources" title={t('mcpResources.title')}>
          <P>{t('mcpResources.lead')}</P>

          <ul className="mt-stack gap-sm flex flex-col">
            {mcp.guide.map((section) => (
              <li key={section.id} className="text-prose">
                <Link
                  href={`/mcp/resources/#${anchorForGuide(section.id)}`}
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
      </Toc>
    </>
  );
}
