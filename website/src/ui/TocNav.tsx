'use client';

import { useEffect, useRef, useState } from 'react';

import type { TocGroup, TocPages } from '@/ui/Toc';

const groupHeading = 'text-fine text-secondary font-semibold tracking-wide uppercase';
const list = 'mt-sm gap-x-md gap-y-xs text-prose flex flex-wrap lg:flex-col';
const linkBase = 'transition-colors duration-[--motion-quick-ms] hover:underline';
const linkOn = 'text-accent';
const linkOff = 'text-secondary hover:text-primary';

/** Where the top of the page effectively is: the header is pinned, so the rung
 *  every `scroll-mt-section` clears is the same line a section becomes current
 *  at. Read from the stylesheet rather than written here, so the two cannot
 *  drift — and 0 in jsdom, which has no layout to spy on anyway. */
function headerOffset(): number {
  const raw = getComputedStyle(document.documentElement).getPropertyValue('--spacing-section');
  const value = Number.parseFloat(raw);

  if (!Number.isFinite(value)) return 0;

  return raw.trim().endsWith('px') ? value : value * 16;
}

/** The sidebar. A client component so it can say which section you are looking
 *  at: a list of thirty-nine anchors with nothing marked is a list you read
 *  rather than a place you navigate from. */
export function TocNav({
  label,
  pages,
  groups,
}: {
  label: string;
  pages?: TocPages;
  groups: readonly TocGroup[];
}) {
  const nav = useRef<HTMLElement>(null);
  const [active, setActive] = useState<string | null>(null);

  /* A string, not the array: `groups` is a new object on every render, and a
     dependency that always changes re-runs the effect on every render. */
  const anchors = groups
    .flatMap((group) => group.items.map((item) => item.href))
    .filter((href) => href.startsWith('#'))
    .join('|');

  useEffect(() => {
    if (anchors === '') return;

    const targets = anchors
      .split('|')
      .map((href) => ({ href, element: document.getElementById(href.slice(1)) }))
      .filter(
        (target): target is { href: string; element: HTMLElement } => target.element !== null,
      );

    if (targets.length === 0) return;

    let frame = 0;

    const measure = () => {
      frame = 0;

      // The LAST one whose top has passed the header, so a section stays
      // current until the next one reaches the same line. Nothing has passed
      // it at the top of the page, where the first entry is the right answer.
      const line = headerOffset() + 1;
      let current = targets[0]?.href ?? null;

      for (const target of targets)
        if (target.element.getBoundingClientRect().top <= line) current = target.href;

      setActive(current);
    };

    const onScroll = () => {
      if (frame === 0) frame = requestAnimationFrame(measure);
    };

    measure();
    window.addEventListener('scroll', onScroll, { passive: true });
    window.addEventListener('resize', onScroll);

    return () => {
      if (frame !== 0) cancelAnimationFrame(frame);
      window.removeEventListener('scroll', onScroll);
      window.removeEventListener('resize', onScroll);
    };
  }, [anchors]);

  /* Keep the marked entry visible in the sidebar's OWN scroll box. Only when
     there is one: below `lg` the groups wrap as rows and are not scrollable,
     and scrolling the window there would fight the reader. */
  useEffect(() => {
    const box = nav.current;

    if (active === null || box === null || box.scrollHeight <= box.clientHeight) return;

    const link = box.querySelector(`a[href="${active}"]`);

    if (link instanceof HTMLElement && typeof link.scrollIntoView === 'function')
      link.scrollIntoView({ block: 'nearest' });
  }, [active]);

  return (
    <nav ref={nav} className="lg:docked" aria-label={label}>
      {pages ? (
        <div data-toc-group="pages">
          <h2 className={groupHeading}>{pages.title}</h2>

          <ul className={list}>
            {pages.items.map((item) => (
              <li key={item.href}>
                <a
                  href={item.href}
                  aria-current={item.current ? 'page' : undefined}
                  className={`${linkBase} ${item.current ? linkOn : linkOff}`}
                >
                  {item.label}
                </a>
              </li>
            ))}
          </ul>
        </div>
      ) : null}

      {groups.map((group, i) => (
        <div
          key={group.id}
          data-toc-group={group.id}
          className={i > 0 || pages !== undefined ? 'mt-stack' : ''}
        >
          <h2 className={groupHeading}>{group.title}</h2>

          <ul className={list}>
            {group.items.map((item) => {
              const on = item.href === active;

              return (
                <li key={item.href}>
                  <a
                    href={item.href}
                    aria-current={on ? 'true' : undefined}
                    className={`${linkBase} ${on ? linkOn : linkOff} ${item.mono ? 'font-mono' : ''}`}
                  >
                    {item.label}
                  </a>
                </li>
              );
            })}
          </ul>
        </div>
      ))}
    </nav>
  );
}
