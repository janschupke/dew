import { Fragment } from 'react';
import type { Metadata } from 'next';

import { Cell, Row, Table, Tag } from '@/ui/Table';
import { Container } from '@/ui/Surface';
import { Doc, Lead } from '@/ui/Prose';
import {
  type ArgSpec,
  anchorForGuide,
  anchorForTool,
  describeKind,
  mcp,
  readTools,
  writeTools,
} from '@/lib/mcp';
import { t } from '@/lib/strings';

export const metadata: Metadata = {
  title: `${t('mcpReference.title')} — ${t('site.name')}`,
};

/*  Generated, end to end. Nothing on this page is typed by hand.

    One page with anchors rather than a route per tool, for the reason the score
    reference is one page: thirty-nine tools of four or five arguments are a
    reference you scan and search, and the commonest reading act is comparative
    - "which of these change the project, and what does each take?". Grouping by
    scope answers that before a word is read.
*/

/** One argument, and the shape of anything nested inside it.
 *
 *  Recursive because the table is: almost every write is batch-first, taking an
 *  array of entries, and an argument list that stopped at the array would
 *  document a blob.
 *
 *  Nesting is shown as the PATH rather than as an indent - `entries[].pitch`
 *  rather than a margin - which reads as the thing a caller actually writes,
 *  and needs no inline style. The site may not carry a length in one anyway.
 */
function ArgumentRows({ args, prefix = '' }: { args: readonly ArgSpec[]; prefix?: string }) {
  return (
    <>
      {args.map((arg) => {
        const path = prefix + arg.name;
        const nested = arg.kind === 'array' ? path + '[].' : path + '.';

        return (
          <Fragment key={path}>
            <Row>
              <Cell className="font-mono">{path}</Cell>
              <Cell className="text-secondary whitespace-nowrap">{describeKind(arg)}</Cell>
              <Cell className="text-secondary">
                {arg.required ? (
                  <span className="mr-sm inline-block">
                    <Tag>{t('mcpReference.required')}</Tag>
                  </span>
                ) : null}
                <Doc>{arg.doc}</Doc>
              </Cell>
            </Row>

            {arg.fields.length > 0 && !arg.holdsBareElements ? (
              <ArgumentRows args={arg.fields} prefix={nested} />
            ) : null}
          </Fragment>
        );
      })}
    </>
  );
}

function ToolSection({ tool }: { tool: (typeof mcp.tools)[number] }) {
  return (
    <section
      id={anchorForTool(tool.name)}
      data-mcp-anchor={anchorForTool(tool.name)}
      className="mt-section scroll-mt-section"
    >
      <h3 className="text-h3 text-primary font-mono">{tool.name}</h3>

      <p className="mt-xs gap-sm text-prose text-secondary flex flex-wrap items-center">
        <Doc>{tool.summary}</Doc>
        <Tag>
          {tool.scope === 'read' ? t('mcpReference.readScope') : t('mcpReference.writeScope')}
        </Tag>
      </p>

      {tool.doc.split('\n\n').map((paragraph, i) => (
        <p key={i} className="mt-sm text-prose leading-prose text-secondary max-w-[68ch]">
          <Doc>{paragraph}</Doc>
        </p>
      ))}

      {tool.args.length === 0 ? (
        <p className="mt-lg text-prose text-secondary">{t('mcpReference.noArguments')}</p>
      ) : (
        <div className="mt-lg">
          <Table
            head={[
              t('mcpReference.argumentsHeading'),
              t('mcpReference.typeHeading'),
              t('mcpReference.notesHeading'),
            ]}
          >
            <ArgumentRows args={tool.args} />
          </Table>
        </div>
      )}
    </section>
  );
}

export default function McpReference() {
  const groups = [
    { title: t('mcpReference.readTitle'), tools: readTools },
    { title: t('mcpReference.writeTitle'), tools: writeTools },
  ] as const;

  return (
    <Container className="py-section">
      <h1 className="text-h1 text-primary leading-tight font-semibold tracking-tight">
        {t('mcpReference.title')}
      </h1>
      <Lead>{t('mcpReference.lead')}</Lead>

      <p className="mt-stack text-fine text-secondary">
        {t('mcpReference.protocol')}: <span className="font-mono">{mcp.protocolVersion}</span>
      </p>

      <nav className="mt-stack" aria-label={t('mcpReference.onThisPage')}>
        <h2 className="text-fine text-secondary font-semibold tracking-wide uppercase">
          {t('mcpReference.onThisPage')}
        </h2>
        <ul className="mt-sm gap-md text-prose flex flex-wrap">
          {mcp.tools.map((tool) => (
            <li key={tool.name}>
              <a
                href={`#${anchorForTool(tool.name)}`}
                className="text-accent font-mono hover:underline"
              >
                {tool.name}
              </a>
            </li>
          ))}
        </ul>
      </nav>

      <section className="py-section">
        <h2 className="text-h2 text-primary font-semibold">{t('mcpReference.guideTitle')}</h2>
        <p className="mt-sm text-prose text-secondary max-w-[68ch]">
          {t('mcpReference.guideLead')}
        </p>

        {mcp.guide.map((section) => (
          <section
            key={section.id}
            id={anchorForGuide(section.id)}
            data-mcp-anchor={anchorForGuide(section.id)}
            className="mt-section scroll-mt-section"
          >
            <h3 className="text-h3 text-primary font-semibold">{section.title}</h3>
            <p className="mt-xs text-fine text-secondary font-mono">dew://guide/{section.id}</p>

            {section.paragraphs.map((paragraph, i) => (
              <p key={i} className="mt-sm text-prose leading-prose text-secondary max-w-[68ch]">
                <Doc>{paragraph}</Doc>
              </p>
            ))}
          </section>
        ))}
      </section>

      <section className="py-section">
        <h2 className="text-h2 text-primary font-semibold">{t('mcpReference.toolsTitle')}</h2>
        <p className="mt-sm text-prose text-secondary max-w-[68ch]">{t('mcpReference.undoNote')}</p>

        {groups.map((group) => (
          <section key={group.title} className="mt-section">
            <h3 className="text-h3 text-primary font-semibold">{group.title}</h3>

            {group.tools.map((tool) => (
              <ToolSection key={tool.name} tool={tool} />
            ))}
          </section>
        ))}
      </section>
    </Container>
  );
}
