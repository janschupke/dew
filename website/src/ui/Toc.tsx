import type { ReactNode } from 'react';

import { Container } from '@/ui/Surface';

/*  The shell every generated reference page wears: a sidebar of where you can
    go, and the page beside it.

    Both reference pages used to carry their own copy of one flat wrapping list
    of links - thirty-nine tool names in a paragraph-shaped pile - which is a
    list you read rather than a place you navigate from, and which scrolled away
    the moment you used it.

    The GROUPS are the argument for this being one component. What a reader
    wants first on the MCP page is "which of these change my project", and on
    the score page "which block am I in"; both are answered by the headings in
    the sidebar before a link is read. A flat list answers neither.

    `data-toc-group` is for the tests. A gate over the sections is not a gate
    over the list of them - the score reference already shipped an index that
    silently omitted a block - so the tests assert both directions, and they
    need to be able to ask for one group without counting the others.
*/
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

export function Toc({
  label,
  groups,
  children,
}: {
  label: string;
  groups: readonly TocGroup[];
  children: ReactNode;
}) {
  return (
    <Container className="pb-section">
      {/* `auto` rather than a width: the column is as wide as the longest name
          in it, which is a measurement the content owns and the ladder has no
          rung for.

          NO `items-start`. It was written with one, which sizes the aside to
          its own contents - and a sticky box can only travel inside its
          containing block, so the sidebar scrolled away with it and the column
          was empty for the rest of the page. The grid's default stretch is what
          gives it the height of the row to stand in. */}
      <div className="gap-gutter pt-stack grid lg:grid-cols-[auto_1fr]">
        <aside>
          {/* Sticky only where there is a column beside it. Below `lg` the
              groups wrap as rows above the page, which is the shape the flat
              list had and the right one when there is nowhere to stand. */}
          <nav className="lg:docked" aria-label={label}>
            {groups.map((group, i) => (
              <div key={group.id} data-toc-group={group.id} className={i > 0 ? 'mt-stack' : ''}>
                <h2 className="text-fine text-secondary font-semibold tracking-wide uppercase">
                  {group.title}
                </h2>

                <ul className="mt-sm gap-x-md gap-y-xs text-prose flex flex-wrap lg:flex-col">
                  {group.items.map((item) => (
                    <li key={item.href}>
                      <a
                        href={item.href}
                        className={`text-accent hover:underline ${item.mono ? 'font-mono' : ''}`}
                      >
                        {item.label}
                      </a>
                    </li>
                  ))}
                </ul>
              </div>
            ))}
          </nav>
        </aside>

        <div className="min-w-0">{children}</div>
      </div>
    </Container>
  );
}
