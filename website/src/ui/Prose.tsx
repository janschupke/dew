import type { ReactNode } from 'react';

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

export function Lead({ children }: { children: ReactNode }) {
  return <p className="text-lead text-secondary max-w-[68ch]">{children}</p>;
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
    <section {...(id ? { id } : {})} className="scroll-mt-xxl py-xxl">
      <h2 className="mb-lg text-h2 text-primary font-semibold">{title}</h2>
      {children}
    </section>
  );
}
