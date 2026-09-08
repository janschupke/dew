import type { Metadata } from 'next';
import Link from 'next/link';

import { P, PageHeader, Section } from '@/ui/Prose';
import { Score } from '@/ui/Score';
import { Toc, type TocGroup } from '@/ui/Toc';
import { scorePages } from '@/content/sections';
import { t } from '@/lib/strings';

export const metadata: Metadata = { title: `${t('score.title')} — ${t('site.name')}` };

const code = 'text-code-small font-mono';

const sections = [
  { id: 'what-it-is', title: t('score.whatTitle') },
  { id: 'no-expressions', title: t('score.noExpressionsTitle') },
  { id: 'durations', title: t('score.durationsTitle') },
  { id: 'chords', title: t('score.chordsTitle') },
  { id: 'limits', title: t('score.limitsTitle') },
] as const;

/*  The narrative. The reference is generated and lives next door. */
export default function ScoreLanguage() {
  const toc: readonly TocGroup[] = [
    {
      id: 'score',
      title: t('score.onThisPage'),
      items: sections.map((section) => ({ href: `#${section.id}`, label: section.title })),
    },
  ];

  return (
    <>
      <PageHeader title={t('score.title')} lead={t('score.lead')} />

      <Toc label={t('score.onThisPage')} pages={scorePages('/score/')} groups={toc}>
        <Section id="what-it-is" title={t('score.whatTitle')}>
          <P>{t('score.whatBody')}</P>
          <Score name="rhodes" lines={[14, 37]} caption={t('score.exampleCaption')} />
        </Section>

        <Section id="no-expressions" title={t('score.noExpressionsTitle')}>
          <P>{t('score.noExpressions')}</P>
        </Section>

        <Section id="durations" title={t('score.durationsTitle')}>
          <P>
            <code className={code}>p/q</code> is always a note value.{' '}
            <code className={code}>1/8t</code> is an eighth-note triplet,{' '}
            <code className={code}>1/4.</code> a dotted quarter. <code className={code}>xN</code> is
            a share of what is left over and anything else is an exact length.
          </P>

          <P className="mt-stack">
            <code className={code}>|</code> is a bar-line <em>assertion</em>, checked once the
            shares are known. It turns “the section length changed and everything shifted” into one
            message.
          </P>

          <Score name="rhodes" lines={[76, 88]} />
        </Section>

        <Section id="chords" title={t('score.chordsTitle')}>
          <P>
            Case carries quality, <code className={code}>^</code> carries inversion and{' '}
            <code className={code}>/</code> carries tonicisation, as in{' '}
            <code className={code}>V7/iv</code>. An uppercase A–G starts an absolute chord; a{' '}
            <code className={code}>b</code>, <code className={code}>#</code> or roman letter starts
            a numeral.
          </P>
        </Section>

        <Section id="limits" title={t('score.limitsTitle')}>
          <P>{t('score.limitsBody')}</P>
        </Section>

        <p className="mt-section text-prose">
          <Link href="/score/reference/" className="text-accent hover:underline">
            {t('score.referenceLink')}
          </Link>
        </p>
      </Toc>
    </>
  );
}
