import Link from 'next/link';
import type { ReactNode } from 'react';

/** A button in one of the application's four roles: a caller says what a button
 *  MEANS rather than which colour it is. The prop is `variant` and not `role`,
 *  which is a real ARIA attribute on an anchor.
 *
 *  `hover:bg-surface-raised-hover` is surfaceRaised lifted by controlLift, and
 *  is NOT `surface-hover` — a darker colour doing a different job.
 *
 *  An internal href goes through next/link; an external one stays an anchor,
 *  which has nothing to prefetch off-origin.
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

/** Not `ComponentPropsWithoutRef<'a'>`: next/link's props are stricter than an
 *  anchor's under `exactOptionalPropertyTypes`, and every call site passes four
 *  things. */
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
