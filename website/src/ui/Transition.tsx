'use client';

import type { ReactNode } from 'react';
import { usePathname } from 'next/navigation';

/** The fade a route change gets, and the site's fourth client component.
 *
 *  A key is the whole mechanism: React unmounts the old subtree and mounts a
 *  new one when the key changes, and a fresh element runs the `route-enter`
 *  animation in globals.css from the start. Nothing here reads a timer, holds
 *  state or measures anything, which is why there is no effect.
 *
 *  The alternative was Next's `experimental.viewTransition`, which is an
 *  unstable flag over a React API this tree is not on; and a bare
 *  `@view-transition` rule, which never fires because the App Router navigates
 *  without swapping the document.
 */
export function RouteFade({ children }: { children: ReactNode }) {
  return (
    <div key={usePathname()} className="route-enter">
      {children}
    </div>
  );
}
