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

/** A page's opening: the h1 and its lead, on the page's own column. */
export function PageHeader({ title, lead }: { title: string; lead: string }) {
  return (
    <Container className="pt-section pb-stack">
      <h1 className="text-h1 text-primary leading-tight font-semibold tracking-tight">{title}</h1>
      <Lead>{lead}</Lead>
    </Container>
  );
}

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
    <section {...(id ? { id } : {})} className="scroll-mt-band py-section">
      <h2 className="mb-stack text-h2 text-primary leading-snug font-semibold tracking-tight">
        {title}
      </h2>
      {children}
    </section>
  );
}
