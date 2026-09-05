import type { Metadata } from 'next';

import { Band, Container } from '@/ui/Surface';
import { GitHubMark } from '@/ui/Icon';
import { P, PageHeader, Section, SectionPair } from '@/ui/Prose';
import { Cell, Row, Table } from '@/ui/Table';
import {
  buildCommands,
  checkCommands,
  presets,
  repositoryUrl,
  requirements,
  runCommands,
} from '@/content/setup';
import { t } from '@/lib/strings';

export const metadata: Metadata = { title: `${t('setup.title')} — ${t('site.name')}` };

/*  How to build it, for somebody who has not cloned it.

    The commands are `src/content/setup.ts` and are asserted against README.md
    by tests/setup.test.ts, so this page cannot drift from the repository it
    describes. The prose around them is the site's, and shorter: the README says
    why the dependency pins are split in two, and that is not what somebody
    reading this wants.

    No shell HIGHLIGHTING, and deliberately. `Score` paints spans over text that
    `dew_shot samples` already tokenised with the compiler's own tokeniser -
    there is no second grammar anywhere on this site, and adding one for `sh`
    would be exactly the thing .ai/rules/website.md refuses.
*/
function Commands({ lines }: { lines: readonly string[] }) {
  return (
    <pre className="bg-well-deep border-hairline border-divider p-stack text-code-body mt-stack overflow-x-auto rounded-md font-mono leading-relaxed">
      <code>
        {lines.map((line) => (
          <span key={line} className="block">
            <span className="text-disabled select-none">$ </span>
            <span className="text-primary">{line}</span>
          </span>
        ))}
      </code>
    </pre>
  );
}

export default function Setup() {
  return (
    <>
      <PageHeader title={t('setup.title')} lead={t('setup.lead')} />

      <Container className="pb-section">
        <Section title={t('setup.requirementsTitle')}>
          <P>{t('setup.requirementsBody')}</P>

          <div className="mt-stack">
            <Table head={[t('setup.toolHeading'), t('setup.needHeading')]}>
              {requirements.map((requirement) => (
                <Row key={requirement.name}>
                  <Cell className="text-primary whitespace-nowrap">{requirement.name}</Cell>
                  <Cell className="text-secondary">{requirement.need}</Cell>
                </Row>
              ))}
            </Table>
          </div>
        </Section>
      </Container>

      <Band>
        <h2 className="text-h2 text-primary leading-snug font-semibold tracking-tight">
          {t('setup.buildTitle')}
        </h2>
        <Commands lines={buildCommands} />
        <P className="mt-stack">{t('setup.buildBody')}</P>

        <h3 className="mt-section text-h3 text-primary leading-snug font-semibold">
          {t('setup.presetsTitle')}
        </h3>

        <div className="mt-stack">
          <Table head={[t('setup.nameHeading'), t('setup.whatHeading')]}>
            {presets.map((preset) => (
              <Row key={preset.name}>
                <Cell className="text-primary font-mono whitespace-nowrap">{preset.name}</Cell>
                <Cell className="text-secondary">{preset.what}</Cell>
              </Row>
            ))}
          </Table>
        </div>
      </Band>

      <Container className="pb-section">
        {/* Full width, and not half of a pair: the run command is a 62-character
            path, and in half a column it scrolls inside its own box - a command
            you have to drag to read is worse than one with space beside it. */}
        <Section title={t('setup.runTitle')}>
          <Commands lines={runCommands} />
          <P className="mt-stack">{t('setup.runBody')}</P>
        </Section>

        {/* These two are short, and stacked they left the right half of the
            page blank twice running. The link is a link and not a button: the
            three on the home page are calls to action, this one names a URL,
            and as the loudest control on the page it outranked the commands the
            page is actually about. */}
        <SectionPair>
          <Section title={t('setup.checkTitle')}>
            <Commands lines={checkCommands} />
            <P className="mt-stack">{t('setup.checkBody')}</P>
          </Section>

          <Section title={t('setup.sourceCta')}>
            <div>
              <a
                href={repositoryUrl}
                className="text-accent text-prose gap-sm inline-flex items-center hover:underline"
              >
                <GitHubMark />
                {repositoryUrl.replace('https://', '')}
              </a>
            </div>
          </Section>
        </SectionPair>
      </Container>
    </>
  );
}
