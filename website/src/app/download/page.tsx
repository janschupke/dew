import type { Metadata } from 'next';

import { Band, Container } from '@/ui/Surface';
import { Button } from '@/ui/Button';
import { P, PageHeader, Section } from '@/ui/Prose';
import { Cell, Row, Table } from '@/ui/Table';
import {
  checksumsAsset,
  downloadUrl,
  downloads,
  releasesUrl,
  verifyCommands,
} from '@/content/download';
import { t } from '@/lib/strings';

export const metadata: Metadata = { title: `${t('download.title')} — ${t('site.name')}` };

/*  Where a stranger gets a build.

    Every link is a static /releases/latest/download/<asset> URL. There is no
    fetch here and there is no generated file: the site fetches nothing at
    runtime by design (next.config.ts says why), api.github.com allows sixty
    requests an hour per IP so one office behind one address would exhaust it
    for everybody in it, and a version baked in at build time would be wrong
    between a version bump deploying and its builds landing.

    The page therefore names no version at all. The releases page has them, the
    DMG's volume has one, and so does the running program.

    The first-launch section is here because none of these builds is signed and
    both desktop systems block one on first run.
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

export default function Download() {
  return (
    <>
      <PageHeader title={t('download.title')} lead={t('download.lead')} />

      <Container className="pb-section">
        <Section title={t('download.getTitle')}>
          <P>{t('download.getBody')}</P>

          <div className="mt-stack">
            <Table
              head={[
                t('download.platformHeading'),
                t('download.fileHeading'),
                t('download.whatHeading'),
              ]}
            >
              {downloads.map((download) => (
                <Row key={download.asset}>
                  <Cell className="text-primary whitespace-nowrap">{download.platform}</Cell>
                  <Cell className="whitespace-nowrap">
                    <a
                      href={downloadUrl(download.asset)}
                      className="text-accent font-mono hover:underline"
                    >
                      {download.asset}
                    </a>
                  </Cell>
                  <Cell>{download.what}</Cell>
                </Row>
              ))}
            </Table>
          </div>

          <div className="mt-section gap-md flex flex-wrap">
            <Button variant="primary" href={downloadUrl(downloads[0]?.asset ?? checksumsAsset)}>
              {downloads[0]?.platform ?? t('download.title')}
            </Button>
            <Button href={releasesUrl}>{t('download.allReleases')}</Button>
          </div>
        </Section>

        <Section title={t('download.firstRunTitle')}>
          <P>{t('download.firstRunBody')}</P>

          <dl className="mt-stack gap-stack grid">
            {downloads.map((download) => (
              <div key={download.asset}>
                <dt className="text-prose text-primary font-semibold">{download.platform}</dt>
                <dd className="mt-xs text-prose leading-prose text-secondary max-w-[68ch]">
                  {download.firstLaunch}
                </dd>
              </div>
            ))}
          </dl>
        </Section>

        <Section title={t('download.verifyTitle')}>
          <P>{t('download.verifyBody')}</P>

          <p className="mt-stack text-prose">
            <a href={downloadUrl(checksumsAsset)} className="text-accent font-mono hover:underline">
              {t('download.checksumsLabel')}
            </a>
          </p>

          <Commands lines={verifyCommands} />
        </Section>
      </Container>

      <Band>
        <Section title={t('download.buildTitle')}>
          <P>{t('download.buildBody')}</P>
          <P className="mt-stack">{t('download.mp3Note')}</P>

          <div className="mt-section">
            <Button href="/setup/">{t('download.buildCta')}</Button>
          </div>
        </Section>
      </Band>
    </>
  );
}
