import type { ReactNode } from 'react';

import { Container } from '@/ui/Surface';
import { TocNav } from '@/ui/TocNav';

export interface TocItem {
  readonly href: string;
  readonly label: string;
  /** A tool name and a block kind are identifiers, and are set as ones. */
  readonly mono?: boolean;
}

export interface TocGroup {
  readonly id: string;
  readonly title: string;
  readonly items: readonly TocItem[];
}

/** The other pages of the same section, with the one being read marked. */
export interface TocPages {
  readonly title: string;
  readonly items: readonly (TocItem & { readonly current?: boolean })[];
}

/** The shell a sectioned page wears: a sidebar of where you can go, and the
 *  page beside it. Each group carries a `data-toc-group` for the tests, which
 *  assert both directions per group. */
export function Toc({
  label,
  pages,
  groups,
  children,
}: {
  label: string;
  pages?: TocPages;
  groups: readonly TocGroup[];
  children: ReactNode;
}) {
  return (
    <Container className="pb-section">
      {/* `auto` rather than a width: the column is as wide as the longest name
          in it. NO `items-start` — that sizes the aside to its own contents, and
          a sticky box can only travel inside its containing block. */}
      <div className="gap-gutter pt-section grid lg:grid-cols-[auto_1fr]">
        {/* `shrinkable`, because a grid item's default `min-width: auto` stops
            it shrinking: below `lg` this column is the whole grid, and the aside
            sized itself to the longest tool name and pushed the page sideways.
            `min-w-0` is the obvious spelling and emits nothing - see
            theme.site.css. */}
        <aside className="shrinkable">
          <TocNav label={label} groups={groups} {...(pages ? { pages } : {})} />
        </aside>

        <div className="shrinkable">{children}</div>
      </div>
    </Container>
  );
}
