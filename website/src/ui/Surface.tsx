import Link from 'next/link';
import type { ReactNode } from 'react';

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

/** A card. WITH an href it is a real link and lifts; without one it is a flat
 *  panel and does not move at all.
 *
 *  `block` is load-bearing: next/link renders an inline anchor, and the
 *  `h-full` the grid call sites pass has nothing to stretch inside one. */
export function Card({
  href,
  className = '',
  children,
}: {
  href?: string;
  className?: string;
  children: ReactNode;
}) {
  const base = `border-hairline border-divider bg-surface p-stack rounded-md ${className}`;

  if (href === undefined) return <div className={base}>{children}</div>;

  return (
    <Link
      href={href}
      className={
        `${base} hover:border-divider-strong hover:bg-surface-hover block ` +
        'transition-colors duration-[--motion-panel-ms]'
      }
    >
      {children}
    </Link>
  );
}
