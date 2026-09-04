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

/** A surface with an edge and a corner: a card. `divider` rather than
 *  `outline`, because a card is separated rather than interactive.
 */
export function Card({ className = '', children }: { className?: string; children: ReactNode }) {
  return (
    <div className={`border-hairline border-divider bg-surface p-xl rounded-md ${className}`}>
      {children}
    </div>
  );
}
