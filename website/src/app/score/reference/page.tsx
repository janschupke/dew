import type { Metadata } from 'next';

import { Cell, Row, Table, Tag } from '@/ui/Table';
import { Doc, Lead } from '@/ui/Prose';
import {
  anchorForBlock,
  anchorForKey,
  anchorForValueKind,
  closedKinds,
  schema,
} from '@/lib/schema';
import { t } from '@/lib/strings';

export const metadata: Metadata = { title: `${t('reference.title')} — ${t('site.name')}` };

/*  Generated, end to end. Nothing on this page is typed by hand.

    One page with anchors rather than a route per block: thirteen blocks of
    three or four keys are a reference you scan and search, not one you navigate,
    and the commonest reading act is comparative - "what does `section` take,
    and which of those can an arrangement override?". Anchors are also permanent
    link targets, which a page tree's URLs are not.
*/
export default function Reference() {
  return (
    <div className="py-xxl">
      <h1 className="text-h1 text-primary font-semibold">{t('reference.title')}</h1>
      <Lead>{t('reference.lead')}</Lead>

      <nav className="mt-xl" aria-label={t('reference.onThisPage')}>
        <h2 className="text-small text-secondary">{t('reference.onThisPage')}</h2>
        <ul className="mt-sm gap-md text-body flex flex-wrap">
          {schema.blocks.map((block) => (
            <li key={block.kind}>
              <a href={`#${anchorForBlock(block.kind)}`} className="text-accent hover:underline">
                {block.kind}
              </a>
            </li>
          ))}
        </ul>
      </nav>

      <section className="py-xxl">
        <h2 className="text-h2 text-primary font-semibold">{t('reference.blocksTitle')}</h2>

        {schema.blocks.map((block) => (
          <section
            key={block.kind}
            id={anchorForBlock(block.kind)}
            data-schema-anchor={anchorForBlock(block.kind)}
            className="mt-xxl scroll-mt-xxl"
          >
            <h3 className="text-title text-primary font-mono">{block.kind}</h3>

            <p className="mt-xs gap-sm text-body text-secondary flex flex-wrap items-center">
              <Doc>{block.doc}</Doc>
              <Tag>{block.topLevel ? t('reference.topLevel') : t('reference.nested')}</Tag>
            </p>

            {block.children.length > 0 ? (
              <p className="mt-sm text-small text-secondary">
                {t('reference.contains')}:{' '}
                {block.children.map((child, i) => (
                  <span key={child}>
                    {i > 0 ? ', ' : ''}
                    <a href={`#${anchorForBlock(child)}`} className="text-accent hover:underline">
                      {child}
                    </a>
                  </span>
                ))}
              </p>
            ) : null}

            {block.keys.length === 0 ? (
              <p className="mt-lg text-body text-secondary">{t('reference.noKeys')}</p>
            ) : (
              <div className="mt-lg">
                <Table
                  head={[
                    t('reference.keyHeading'),
                    t('reference.valueHeading'),
                    t('reference.notesHeading'),
                  ]}
                >
                  {block.keys.map((key) => (
                    <Row key={key.name} id={anchorForKey(block.kind, key.name)}>
                      <Cell className="text-primary font-mono">{key.name}</Cell>
                      <Cell className="text-secondary">
                        <a
                          href={`#${anchorForValueKind(key.kind)}`}
                          className="text-accent hover:underline"
                        >
                          {key.kind}
                        </a>
                      </Cell>
                      <Cell className="text-secondary">
                        <Doc>{key.doc}</Doc>
                        <span className="ml-sm gap-xs inline-flex">
                          {key.required ? <Tag>{t('reference.required')}</Tag> : null}
                          {key.overridable ? <Tag>{t('reference.overridable')}</Tag> : null}
                        </span>
                      </Cell>
                    </Row>
                  ))}
                </Table>
              </div>
            )}
          </section>
        ))}
      </section>

      <section className="py-xxl">
        <h2 className="text-h2 text-primary font-semibold">{t('reference.valuesTitle')}</h2>
        <p className="mt-sm text-body text-secondary">
          {t('reference.kindsCount', { count: schema.valueKinds.length })}
        </p>

        <div className="mt-lg">
          <Table head={[t('reference.valueHeading'), t('reference.notesHeading')]}>
            {schema.valueKinds.map((kind) => (
              <Row key={kind.name} id={anchorForValueKind(kind.name)}>
                <Cell className="text-primary font-mono">{kind.name}</Cell>
                <Cell className="text-secondary">
                  <Doc>{kind.doc}</Doc>
                  {kind.members.length > 0 ? (
                    <span className="mt-xs gap-xs flex flex-wrap">
                      {kind.members.map((member) => (
                        <code
                          key={member}
                          className="bg-well px-xs text-code-small text-playhead rounded-xs font-mono"
                        >
                          {member}
                        </code>
                      ))}
                    </span>
                  ) : null}
                </Cell>
              </Row>
            ))}
          </Table>
        </div>

        <p className="mt-lg text-small text-secondary">
          {closedKinds.length} of them accept a closed list of members.
        </p>
      </section>

      <section className="py-xxl">
        <h2 className="text-h2 text-primary font-semibold">{t('reference.modesTitle')}</h2>

        <div className="mt-lg">
          <Table head={['', t('reference.degrees'), t('reference.romanNumerals')]}>
            {schema.modes.map((mode) => (
              <Row key={mode.name}>
                <Cell className="text-primary font-mono">{mode.name}</Cell>
                <Cell className="text-secondary font-mono">{mode.degrees.join(' ')}</Cell>
                <Cell className="text-secondary">{mode.romanNumerals ? 'yes' : 'no'}</Cell>
              </Row>
            ))}
          </Table>
        </div>

        <p className="mt-lg text-small text-secondary max-w-[68ch]">
          A roman numeral names a scale degree, so a pentatonic or a blues scale cannot carry one —
          and saying so is better than silently indexing past the end of a five-note table.
        </p>
      </section>

      <p className="text-small text-disabled">{t('reference.generatedFrom')}</p>
    </div>
  );
}
