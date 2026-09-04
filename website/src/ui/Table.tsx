import type { ReactNode } from 'react';

/** The reference tables. Rows separated by `divider`, the header by
 *  `dividerStrong` - the same pair the app draws a grid line and a bar line
 *  with, doing the same job of saying which rule is structural.
 *
 *  Wrapped in its own overflow container so a wide table scrolls inside itself
 *  rather than making the page scroll sideways.
 */
export function Table({ head, children }: { head: readonly string[]; children: ReactNode }) {
  return (
    <div className="overflow-x-auto">
      <table className="text-prose w-full border-collapse text-left leading-snug">
        <thead>
          <tr className="border-b-hairline border-divider-strong">
            {head.map((cell) => (
              <th
                key={cell}
                className="py-md pr-stack text-fine text-secondary font-semibold tracking-wide uppercase"
              >
                {cell}
              </th>
            ))}
          </tr>
        </thead>
        <tbody>{children}</tbody>
      </table>
    </div>
  );
}

export function Row({ id, children }: { id?: string; children: ReactNode }) {
  return (
    <tr
      {...(id ? { id } : {})}
      data-schema-anchor={id ?? undefined}
      className="border-b-hairline border-divider scroll-mt-section last:border-0"
    >
      {children}
    </tr>
  );
}

export function Cell({ className = '', children }: { className?: string; children: ReactNode }) {
  return <td className={`py-md pr-stack align-top ${className}`}>{children}</td>;
}

/** A short flag beside a key: required, overridable, top level. Reads as a
 *  label rather than a control, so it takes the smallest radius on the ladder.
 */
export function Tag({ children }: { children: ReactNode }) {
  return (
    <span className="bg-surface-raised px-sm py-xxs text-fine text-secondary rounded-xs">
      {children}
    </span>
  );
}
