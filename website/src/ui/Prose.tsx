import type { ReactNode } from 'react';

import { Container } from '@/ui/Surface';

/** A schema doc string, rendered.
 *
 *  The `doc` strings in src/lang/Schema.cpp carry markdown code spans -
 *  "steps per beat, or `auto` to derive it from the durations written" - so a
 *  reference cell that printed them raw would show the backticks. This is the
 *  only markdown the site parses, and one span is the whole of it: anything
 *  more would be a second content pipeline for thirteen sentences.
 */
export function Doc({ children }: { children: string }) {
  const parts = children.split('`');

  return (
    <>
      {parts.map((part, i) =>
        i % 2 === 1 ? (
          <code key={i} className="bg-well px-xs text-code-small rounded-xs font-mono">
            {part}
          </code>
        ) : (
          <span key={i}>{part}</span>
        ),
      )}
    </>
  );
}

/** Body copy, at the measure. Every paragraph on the site that is meant to be
 *  READ goes through this or carries the same three classes: `text-prose`,
 *  `leading-prose` and a 68ch column. */
export function P({ className = '', children }: { className?: string; children: ReactNode }) {
  return (
    <p className={`text-prose leading-prose text-secondary max-w-[68ch] ${className}`}>
      {children}
    </p>
  );
}

export function Lead({ children }: { children: ReactNode }) {
  return <p className="text-lead text-secondary mt-stack max-w-[68ch] leading-snug">{children}</p>;
}

/** A page's opening: the h1 and its lead, on the page's own column.
 *
 *  No bottom padding. Whatever follows carries its own top rung - a Section, a
 *  Band, a figure - and a rung here as well put two of them between the lead
 *  and the first heading on every page on the site.
 */
export function PageHeader({ title, lead }: { title: string; lead: string }) {
  return (
    <Container className="pt-section">
      <h1 className="text-h1 text-primary leading-tight font-semibold tracking-tight">{title}</h1>
      <Lead>{lead}</Lead>
    </Container>
  );
}

/** A section, with the air ABOVE it and none below.
 *
 *  It was `py-section`, which meant two adjacent sections put two rungs between
 *  them and a page of six of them was mostly gap. Rhythm in one direction only
 *  is the fix: the space between two sections is one rung whatever the rung is
 *  worth, and the last one's trailing air belongs to the Container it sits in,
 *  which is the element that knows where the page ends.
 *
 *  `scroll-mt-section` rather than `scroll-mt-band`, and the same rung
 *  everywhere an anchor lands: the header is pinned, so every target has to
 *  clear it, and three different rungs doing that job was three chances to pick
 *  one that does not.
 */
export function Section({
  id,
  title,
  children,
}: {
  id?: string;
  title: string;
  children: ReactNode;
}) {
  return (
    <section {...(id ? { id } : {})} className="scroll-mt-section pt-section">
      <h2 className="mb-stack text-h2 text-primary leading-snug font-semibold tracking-tight">
        {title}
      </h2>
      {children}
    </section>
  );
}

/** Two sections side by side, stacked below `lg`.
 *
 *  A section's copy is capped at the 68ch measure and the page is 72rem wide,
 *  so two SHORT ones in a row left the right half of the page empty twice
 *  running. Each column here is about 62ch, still a measure, so the cap simply
 *  stops binding inside one.
 *
 *  Only for a pair that is genuinely short. A section carrying a shot, a wide
 *  table or a block of commands still wants the whole column, and halving it
 *  would trade one kind of waste for a worse one.
 */
export function SectionPair({ children }: { children: ReactNode }) {
  return <div className="gap-x-gutter grid lg:grid-cols-2">{children}</div>;
}
