import type { Metadata } from 'next';
import Link from 'next/link';

import { Container } from '@/ui/Surface';
import { PageHeader, Section, SectionPair } from '@/ui/Prose';
import { Score } from '@/ui/Score';
import { t } from '@/lib/strings';

export const metadata: Metadata = { title: `${t('score.title')} — ${t('site.name')}` };

/*  The narrative. The REFERENCE is generated and lives next door.

    This is deliberately a shorter, user-facing telling than README.md's own
    section: per .ai/rules/README.md the README owns the reasoning - why the
    parser is hand-written, why the RNG is written out, what was cut - and this
    owns the claim. Where a reader wants the argument, it links.
*/
export default function ScoreLanguage() {
  return (
    <>
      <PageHeader title={t('score.title')} lead={t('score.lead')} />

      <Container className="pb-section">
        <Score name="amber" lines={[7, 30]} caption={t('score.exampleCaption')} />

        <Section title="No expressions">
          <p className="text-prose leading-prose text-secondary max-w-[68ch]">
            {t('score.noExpressions')}
          </p>
        </Section>

        <Section title="Durations">
          <p className="text-prose leading-prose text-secondary max-w-[68ch]">
            <code className="text-code-small font-mono">p/q</code> is always a note value.{' '}
            <code className="text-code-small font-mono">1/8t</code> is an eighth-note triplet,{' '}
            <code className="text-code-small font-mono">1/4.</code> a dotted quarter.{' '}
            <code className="text-code-small font-mono">xN</code> is a share of what is left over
            and anything else is an exact length. They are different token kinds, so the two cannot
            be confused.
          </p>

          <p className="mt-stack text-prose leading-prose text-secondary max-w-[68ch]">
            <code className="text-code-small font-mono">|</code> is a bar-line <em>assertion</em>,
            checked once the shares are known. It turns “the section length changed and everything
            shifted” into one message, which is the most useful thing in the syntax.
          </p>

          <Score name="amber" lines={[46, 52]} />
        </Section>

        {/* Prose only, and both short. Durations keeps the whole column: it
            carries a sample, and half a column is not enough for one. */}
        <SectionPair>
          <Section title="Chords">
            <p className="text-prose leading-prose text-secondary max-w-[68ch]">
              Case carries quality, <code className="text-code-small font-mono">^</code> carries
              inversion and <code className="text-code-small font-mono">/</code> carries
              tonicisation — using <code className="text-code-small font-mono">^</code> for the
              inversion is what frees <code className="text-code-small font-mono">/</code> for{' '}
              <code className="text-code-small font-mono">V7/iv</code>. An uppercase A–G starts an
              absolute chord and a <code className="text-code-small font-mono">b</code>,{' '}
              <code className="text-code-small font-mono">#</code> or roman letter starts a numeral,
              which never collide because I and V are not note letters.
            </p>
          </Section>

          <Section title="What it cannot say">
            <p className="text-prose leading-prose text-secondary max-w-[68ch]">
              dew stores one tempo and one meter for a whole project, so a section in 3/4 inside a
              4/4 project is not expressible, and the compiler says so in those words instead of as
              a grammar error. A note’s position is a whole number of steps, so the grid is the
              least common multiple of what the durations need: sixteenths and eighth-note triplets
              meet at twelve, and a thirty-second against any triplet needs twenty-four and is
              refused, naming <em>both</em> durations, because either alone would have been fine.
            </p>
          </Section>
        </SectionPair>

        <p className="mt-stack text-prose">
          <Link href="/score/reference/" className="text-accent hover:underline">
            {t('score.referenceLink')}
          </Link>
        </p>
      </Container>
    </>
  );
}
