import Link from 'next/link';
import type { ReactNode } from 'react';

import { ArrowMark } from '@/ui/Icon';

/** A panel, at one rung of the application's surface ladder: `well-deep` is
 *  what a grid sits inside, `well` a recess, `background` the window, and the
 *  three surface rungs are panels and the controls on them. */
export type SurfaceLevel = 'well-deep' | 'well' | 'background' | 'surface' | 'surface-raised';

const levels: Record<SurfaceLevel, string> = {
  'well-deep': 'bg-well-deep',
  well: 'bg-well',
  background: 'bg-background',
  surface: 'bg-surface',
  'surface-raised': 'bg-surface-raised',
};

/** The measure of the page: one width, one gutter, in one place. Separate from
 *  `Band` because a band paints edge to edge while its contents stay on this
 *  column. */
export function Container({
  className = '',
  children,
}: {
  className?: string;
  children: ReactNode;
}) {
  return <div className={`px-gutter mx-auto max-w-[72rem] ${className}`}>{children}</div>;
}

/** A full-bleed horizontal band. `well` is the recessed rung, so a band reads
 *  as set INTO the page: the emphasis transforms encode "less is darker", and a
 *  band that got lighter would read as hovered.
 *
 *  Its padding is INSIDE its border, so whatever precedes one has to bring its
 *  own bottom rung. */
export function Band({
  level = 'well',
  className = '',
  children,
}: {
  level?: SurfaceLevel;
  className?: string;
  children: ReactNode;
}) {
  return (
    <section className={`${levels[level]} border-y-hairline border-divider ${className}`}>
      <Container className="py-band">{children}</Container>
    </section>
  );
}

/** A card, and the two of them do not look alike standing still.
 *
 *  WITH an href it is a real link: it sits a rung higher on the surface ladder,
 *  takes the stronger rule, and wears an arrow. WITHOUT one it is a flat panel,
 *  a rung lower, with the whisper stroke, and it does not answer the pointer.
 *
 *  The distinction used to be the hover alone, which meant the two were
 *  identical until you pointed at one — a reader scanning the page could not
 *  tell which of the nine boxes on the home page went anywhere. The lift is
 *  what the difference reads as, and it is legible on the band and on the page
 *  background alike because "less is darker" holds on both.
 *
 *  The link is a flex box rather than the `block` it used to be, for the same
 *  reason `block` was there: next/link renders an INLINE anchor, and the
 *  `h-full` the grid call sites pass has nothing to stretch inside one.
 */
export function Card({
  href,
  className = '',
  children,
}: {
  href?: string;
  className?: string;
  children: ReactNode;
}) {
  const base = `p-stack rounded-md ${className}`;

  if (href === undefined)
    return <div className={`${base} border-whisper border-divider bg-surface`}>{children}</div>;

  return (
    <Link
      href={href}
      className={
        `${base} border-hairline border-divider-strong bg-surface-raised ` +
        'gap-md flex items-start justify-between ' +
        'hover:bg-surface-raised-hover transition-colors duration-[--motion-panel-ms]'
      }
    >
      <div className="shrinkable">{children}</div>
      <ArrowMark className="text-accent size-xl mt-xs" />
    </Link>
  );
}
