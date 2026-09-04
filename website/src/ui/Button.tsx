import type { ComponentPropsWithoutRef, ReactNode } from 'react';

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
  'px-lg py-sm text-body font-sans transition-colors duration-[--motion-quick-ms]';

type Props = ComponentPropsWithoutRef<'a'> & {
  variant?: ButtonVariant;
  children: ReactNode;
};

export function Button({ variant = 'normal', className = '', children, ...rest }: Props) {
  return (
    <a {...rest} className={`${base} ${variants[variant]} ${className}`}>
      {children}
    </a>
  );
}
