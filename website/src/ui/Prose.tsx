import type { ReactNode } from 'react';

import { Container } from '@/ui/Surface';

/** A generated doc string, rendered. Backtick code spans are the only markdown
 *  the site parses, and are the whole of it. */
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

/** Body copy, at the measure: `text-prose`, `leading-prose`, a 68ch column. */
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

/** A page's opening: the h1 and its lead, used by every page on the site.
 *
 *  No bottom padding — whatever follows carries its own top rung. A `Band` does
 *  not, because its padding sits inside its border, so nothing may follow this
 *  with one directly. `pages.test.tsx` holds both halves. */
export function PageHeader({ title, lead }: { title: string; lead: string }) {
  return (
    <Container className="pt-section">
      <h1 className="text-h1 text-primary leading-tight font-semibold tracking-tight">{title}</h1>
      <Lead>{lead}</Lead>
    </Container>
  );
}

/** A section, with the air ABOVE it and none below: the gap between two is one
 *  rung, and the last one's trailing air belongs to the Container.
 *
 *  `scroll-mt-section` everywhere an anchor lands, because the header is pinned
 *  and every target has to clear it by the same rung the sidebar spies on. */
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
