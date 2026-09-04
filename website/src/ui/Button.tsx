import Link from 'next/link';
import type { ReactNode } from 'react';

/** A button in one of the design system's four roles.
 *
 *  The same four DewButton has (src/ui/primitives/DewControls.cpp), and for the
 *  same reason: a caller says what a button MEANS rather than which colour it
 *  is. Each row below is that paintButton switch, in CSS.
 *
 *  The prop is `variant`, not `role`, only because `role` is a real ARIA
 *  attribute on an anchor and a prop of that name shadows it - the vocabulary
 *  is still the app's.
 *
 *  Note `hover:bg-surface-raised-hover` on `normal`: that token is
 *  surfaceRaised lifted by emphasis::controlLift, which is #3e4148. It is NOT
 *  `surface-hover` (#343941), which is a darker colour doing a different job.
 *  Reaching for the token whose name says "hover" is the obvious mistake here,
 *  and it renders visibly wrong.
 *
 *  An INTERNAL href goes through next/link. This rendered a bare anchor, so the
 *  two calls to action on the home page - the two links most likely to be the
 *  first thing anybody clicks - threw the loaded application away and fetched
 *  the next page from scratch. An external one stays an anchor: next/link has
 *  nothing to prefetch off-origin.
 */
export type ButtonVariant = 'normal' | 'primary' | 'ghost' | 'danger';

const variants: Record<ButtonVariant, string> = {
  normal:
    'bg-surface-raised text-primary border-outline ' +
    'hover:bg-surface-raised-hover active:bg-surface-raised-press',
  primary:
    'bg-accent-muted text-on-accent border-accent ' +
    'hover:bg-accent-muted-hover active:bg-accent-muted-press',
  ghost:
    'border-transparent bg-transparent text-secondary ' +
    'hover:bg-surface-raised active:bg-surface-raised-press',
  danger:
    'bg-surface-raised text-danger border-danger/(--emphasis-dimmed) ' +
    'hover:bg-surface-raised-hover active:bg-surface-raised-press',
};

const base =
  'inline-flex items-center justify-center rounded-sm border-hairline ' +
  'px-stack py-md text-prose font-sans transition-colors duration-[--motion-quick-ms]';

/** Deliberately not `ComponentPropsWithoutRef<'a'>`.
 *
 *  next/link's props are stricter than an anchor's under
 *  `exactOptionalPropertyTypes`, so spreading three hundred optional anchor
 *  attributes into it does not type - and every call site passes four things.
 *  A button on this site is a link with a role; it is not an escape hatch to
 *  the whole anchor surface.
 */
interface Props {
  variant?: ButtonVariant;
  href: string;
  className?: string;
  children: ReactNode;
}

const isInternal = (href: string) => href.startsWith('/');

export function Button({ variant = 'normal', href, className = '', children }: Props) {
  const classes = `${base} ${variants[variant]} ${className}`;

  if (isInternal(href)) {
    return (
      <Link href={href} className={classes}>
        {children}
      </Link>
    );
  }

  return (
    <a href={href} className={classes}>
      {children}
    </a>
  );
}
