import type { ReactNode } from 'react';

/** A panel, at one rung of the surface ladder.
 *
 *  The six rungs are the app's, in the same order: wellDeep is what a grid or a
 *  timeline sits inside, well is a recess, background is the window, and the
 *  three surface rungs are panels and the controls on them.
 */
export type SurfaceLevel = 'well-deep' | 'well' | 'background' | 'surface' | 'surface-raised';

const levels: Record<SurfaceLevel, string> = {
  'well-deep': 'bg-well-deep',
  well: 'bg-well',
  background: 'bg-background',
  surface: 'bg-surface',
  'surface-raised': 'bg-surface-raised',
};

export function Surface({
  level = 'surface',
  className = '',
  children,
}: {
  level?: SurfaceLevel;
  className?: string;
  children: ReactNode;
}) {
  return <div className={`${levels[level]} ${className}`}>{children}</div>;
}

/** The measure of the page: one width, one gutter, in one place.
 *
 *  It was written out three times - in the layout's `<main>`, in the nav and in
 *  the footer - which is the shape of thing that ends up being two widths. It
 *  is a component now because `Band` needs to paint edge to edge while its
 *  contents stay on the same column as everything else, and that is only
 *  possible once the two are separable.
 */
export function Container({
  className = '',
  children,
}: {
  className?: string;
  children: ReactNode;
}) {
  return <div className={`px-gutter mx-auto max-w-[72rem] ${className}`}>{children}</div>;
}

/** A full-bleed horizontal band, at one rung of the surface ladder.
 *
 *  The site used to be one flat `background` from the nav to the footer, with
 *  nothing but a heading saying where one section stopped and the next began.
 *  The application does not have that problem - it is panels inside wells, and
 *  a person reads the structure off the surfaces before reading a word of it -
 *  and the same six rungs work here for the same reason.
 *
 *  `well` is the recessed rung, so a banded section reads as set INTO the page
 *  rather than raised off it. That is the direction the app's ladder goes and
 *  the direction the emphasis transforms encode; a band that got lighter would
 *  read as hovered.
 */
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

/** A surface with an edge and a corner: a card. `divider` rather than
 *  `outline`, because a card is separated rather than interactive.
 *
 *  It lifts under the pointer even though it is not a control, and that is the
 *  one place this departs from the app: on a page a card is usually a link to
 *  somewhere, and a surface that never answers the pointer reads as a picture
 *  of a card. `surface-hover` is the rung the app moves a panel to, so the
 *  movement is the app's even where the reason is the page's.
 */
export function Card({ className = '', children }: { className?: string; children: ReactNode }) {
  return (
    <div
      className={
        'border-hairline border-divider bg-surface p-stack rounded-md ' +
        'hover:border-divider-strong hover:bg-surface-hover ' +
        'transition-colors duration-[--motion-panel-ms] ' +
        className
      }
    >
      {children}
    </div>
  );
}
