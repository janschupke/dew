import type { Metadata } from 'next';

import { Doc, PageHeader } from '@/ui/Prose';
import { Toc } from '@/ui/Toc';
import { anchorForGuide, mcp } from '@/lib/mcp';
import { mcpPages } from '@/content/sections';
import { t } from '@/lib/strings';

export const metadata: Metadata = {
  title: `${t('mcpResources.title')} — ${t('site.name')}`,
};

/*  Generated, end to end. Nothing on this page is typed by hand.

    These are the documents dew serves as MCP resources, in the same words the
    server sends - so a client acting on a paragraph and a person reading one
    are reading the same text. They were the middle third of /mcp/reference/,
    between the tool index and the tools themselves.

    Their anchors are unchanged, so a link written against the old page still
    names the section it always did; only the path in front of it moved.
*/
export default function McpResources() {
  const toc = [
    {
      id: 'resources',
      title: t('mcpResources.title'),
      items: mcp.guide.map((section) => ({
        href: `#${anchorForGuide(section.id)}`,
        label: section.title,
      })),
    },
  ];

  return (
    <>
      <PageHeader title={t('mcpResources.title')} lead={t('mcpResources.lead')} />

      <Toc label={t('mcpResources.onThisPage')} pages={mcpPages('/mcp/resources/')} groups={toc}>
        {mcp.guide.map((section) => (
          <section
            key={section.id}
            id={anchorForGuide(section.id)}
            data-mcp-anchor={anchorForGuide(section.id)}
            className="pt-section scroll-mt-section"
          >
            <h2 className="text-h2 text-primary font-semibold">{section.title}</h2>
            <p className="mt-xs text-fine text-secondary font-mono">dew://guide/{section.id}</p>

            {section.paragraphs.map((paragraph, i) => (
              <p key={i} className="mt-sm text-prose leading-prose text-secondary max-w-[68ch]">
                <Doc>{paragraph}</Doc>
              </p>
            ))}
          </section>
        ))}
      </Toc>
    </>
  );
}
