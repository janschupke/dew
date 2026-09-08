import type { Metadata } from 'next';

import { Band, Container } from '@/ui/Surface';
import { GitHubMark, PlatformMark } from '@/ui/Icon';
import { P, PageHeader, Section } from '@/ui/Prose';
import { Cell, Row, Table } from '@/ui/Table';
import { type Platform, checkCommands, platforms, presets, repositoryUrl } from '@/content/setup';
import { t } from '@/lib/strings';

export const metadata: Metadata = { title: `${t('setup.title')} — ${t('site.name')}` };

/*  The commands are src/content/setup.ts and are asserted against README.md by
    tests/setup.test.ts, so this page cannot drift from the repository it
    describes.

    No shell highlighting, and deliberately: there is no second grammar
    anywhere on this site. */
function Commands({ lines }: { lines: readonly string[] }) {
  return (
    <pre className="bg-well-deep border-hairline border-divider p-stack text-code-body mt-stack overflow-x-auto rounded-md font-mono leading-relaxed">
      <code>
        {/* One prompt per COMMAND, and the text is the command verbatim. A
            command wrapped over continuation lines is still one command, so a
            `$` on its second line would say it is two — and re-indenting those
            lines here would put something on the page that is not what the
            reader would copy. */}
        {lines.map((line) => (
          <span key={line} className="block whitespace-pre">
            <span className="text-disabled select-none">$ </span>
            <span className="text-primary">{line}</span>
          </span>
        ))}
      </code>
    </pre>
  );
}

function PlatformBlock({ platform }: { platform: Platform }) {
  return (
    <section id={platform.system} className="scroll-mt-section pt-section">
      <h2 className="gap-md text-h2 text-primary flex items-center leading-snug font-semibold tracking-tight">
        <PlatformMark system={platform.system} />
        {platform.name}
      </h2>

      <P className="mt-sm">{platform.what}</P>

      <div className="mt-stack">
        <Table head={[t('setup.needsTitle'), t('setup.whatHeading')]}>
          {platform.requirements.map((requirement) => (
            <Row key={requirement.name}>
              <Cell className="text-primary">{requirement.name}</Cell>
              <Cell className="text-secondary">{requirement.need}</Cell>
            </Row>
          ))}
        </Table>
      </div>

      <h3 className="mt-section text-h3 text-primary leading-snug font-semibold">
        {t('setup.buildTitle')}
      </h3>
      <Commands lines={platform.build} />
      <P className="mt-stack">{platform.note}</P>

      <h3 className="mt-section text-h3 text-primary leading-snug font-semibold">
        {t('setup.runTitle')}
      </h3>
      <Commands lines={platform.run} />
    </section>
  );
}

export default function Setup() {
  return (
    <>
      <PageHeader title={t('setup.title')} lead={t('setup.lead')} />

      <Container className="pb-section">
        {platforms.map((platform) => (
          <PlatformBlock key={platform.system} platform={platform} />
        ))}
      </Container>

      <Band>
        <h2 className="text-h2 text-primary leading-snug font-semibold tracking-tight">
          {t('setup.presetsTitle')}
        </h2>
        <P className="mt-stack">{t('setup.presetsBody')}</P>

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
        <Section title={t('setup.checkTitle')}>
          <Commands lines={checkCommands} />
          <P className="mt-stack">{t('setup.checkBody')}</P>
        </Section>

        <Section title={t('setup.sourceCta')}>
          <P>{t('setup.mp3Note')}</P>

          <p className="mt-stack">
            <a
              href={repositoryUrl}
              className="text-accent text-prose gap-sm inline-flex items-center hover:underline"
            >
              <GitHubMark />
              {repositoryUrl.replace('https://', '')}
            </a>
          </p>
        </Section>
      </Container>
    </>
  );
}
