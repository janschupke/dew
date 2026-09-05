import type { Metadata } from 'next';

import { Band, Container } from '@/ui/Surface';
import { Button } from '@/ui/Button';
import { P, PageHeader, Section } from '@/ui/Prose';
import { Cell, Row, Table } from '@/ui/Table';
import { PlatformMark } from '@/ui/Icon';
import {
  checksumsAsset,
  downloadUrl,
  downloads,
  primaryDownloads,
  releasesUrl,
  verifyCommands,
} from '@/content/download';
import { systemName } from '@/content/systems';
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

    The platform mark is on the row, on the first-launch heading and on the
    button, all keyed off `Download.system`. It is aria-hidden everywhere: the
    words are beside it in all three places, and a reader who cannot see it has
    lost nothing.
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

          {/* The buttons come before the table. Most readers want the file for
              the system they are on and nothing else; the table is for the one
              deciding between an installer and a zip. */}
          <div className="mt-stack gap-md flex flex-wrap">
            {primaryDownloads.map((download) => (
              <Button
                key={download.system}
                variant="primary"
                href={downloadUrl(download.asset)}
                className="gap-sm"
              >
                <PlatformMark system={download.system} />
                {systemName(download.system)}
              </Button>
            ))}

            <Button href={releasesUrl}>{t('download.allReleases')}</Button>
          </div>

          <div className="mt-section">
            <Table
              head={[
                t('download.platformHeading'),
                t('download.fileHeading'),
                t('download.whatHeading'),
              ]}
            >
              {downloads.map((download) => (
                <Row key={download.asset}>
                  <Cell className="text-primary whitespace-nowrap">
                    <span className="gap-sm inline-flex items-center">
                      <PlatformMark system={download.system} />
                      {download.platform}
                    </span>
                  </Cell>
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
        </Section>

        <Section title={t('download.firstRunTitle')}>
          <P>{t('download.firstRunBody')}</P>

          <dl className="mt-stack gap-stack grid">
            {downloads.map((download) => (
              <div key={download.asset}>
                <dt className="text-prose text-primary gap-sm flex items-center font-semibold">
                  <PlatformMark system={download.system} />
                  {download.platform}
                </dt>
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
