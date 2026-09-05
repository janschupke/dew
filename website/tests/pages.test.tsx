import { existsSync, readdirSync } from 'node:fs';
import { join } from 'node:path';

import { render, screen } from '@testing-library/react';
import { describe, expect, it } from 'vitest';

import Features from '@/app/features/page';
import Home from '@/app/page';
import Setup from '@/app/setup/page';
import { buildCommands, presets, runCommands } from '@/content/setup';
import { anchorForFeature, features } from '@/content/features';

/*  Every page here is a plain synchronous component and stays one.
    Testing Library cannot render an async server component, so a page that
    reached for `await` would be a page nothing on this side could see.
*/
describe('pages', () => {
  it('the home page states what dew is, and that it is a prototype', () => {
    render(<Home />);

    expect(screen.getByRole('heading', { level: 1 })).toHaveTextContent('dew');
    expect(screen.getByText(/working prototype/i)).toBeInTheDocument();
  });

  it('the features page renders every feature', () => {
    render(<Features />);

    // The coverage half: a feature added to the content module and not to the
    // page is exactly the omission nobody notices.
    for (const feature of features)
      expect(screen.getByRole('heading', { name: feature.name })).toBeInTheDocument();

    expect(features.length).toBeGreaterThan(8);
  });

  it('a feature with a shot puts the sentence beside the picture', () => {
    // The page used to stack them: one sentence, then a 16:10 render tall
    // enough that the next feature started below the fold. The two are columns
    // now, which is a structure rather than a class - the section holds the
    // prose block and the figure as its only two children, in that order.
    const { container } = render(<Features />);

    const withShot = features.filter((feature) => feature.shot !== undefined);

    expect(withShot.length).toBeGreaterThan(3);

    for (const feature of withShot) {
      const section = container.querySelector(`#${CSS.escape(anchorForFeature(feature.name))}`);
      const children = [...(section?.children ?? [])];

      expect(children, feature.name).toHaveLength(2);
      expect(children[0]?.querySelector('h2')?.textContent).toBe(feature.name);
      expect(children[1]?.tagName).toBe('FIGURE');
    }
  });

  it('no feature is left without a sentence', () => {
    for (const feature of features) expect(feature.body.length).toBeGreaterThan(40);
  });

  it('every card on the home page goes somewhere the features page has', () => {
    // The cards used to be divs that lifted under the pointer and did nothing
    // when clicked. They are links now, and the destination is derived on both
    // sides from the same name - so what can still break is a feature renamed
    // on one side of a list the two pages split differently. The home page
    // shows the first six, which straddles that split.
    const anchors = features.slice(0, 6).map((feature) => anchorForFeature(feature.name));

    const home = render(<Home />);

    // Matched on the FRAGMENT, not the whole href. next/link normalises the
    // trailing slash and `trailingSlash: true` is a next.config setting nothing
    // here applies, so the path half differs between this and the export - and
    // the half that carries the meaning is the same in both.
    for (const [i, anchor] of anchors.entries()) {
      const card = home.container.querySelector(`a[href$="#${anchor}"]`);

      expect(card, anchor).not.toBeNull();
      expect(card?.getAttribute('href')).toContain('/features');
      expect(card?.textContent).toContain(features[i]?.name ?? '');
    }

    home.unmount();

    const { container } = render(<Features />);

    for (const anchor of anchors)
      expect(container.querySelector(`#${anchor}`), anchor).not.toBeNull();
  });

  it('no card without a destination answers the pointer', () => {
    // The other half of the rule. The features page's leftover cards and the
    // MCP page's four have nowhere to go, so they are flat panels: a surface
    // that lights up and does nothing is a promise the page cannot keep.
    const { container } = render(<Features />);
    const flat = container.querySelectorAll('div.bg-surface');

    // It cannot pass by finding none.
    expect(flat.length).toBeGreaterThan(3);

    for (const card of flat) expect(card.className, card.textContent).not.toContain('hover:');
  });

  it('the setup page shows the commands and every preset', () => {
    // The page a reader is sent to from the home page's first button, and the
    // only place on the site that says how to get dew at all.
    const { container } = render(<Setup />);

    // textContent rather than getByText: the brew line carries the column of
    // spaces that lines its comment up in the README, and getByText collapses
    // runs of whitespace - so it would pass against a command that had been
    // reformatted, which is one of the two things this is guarding.
    for (const command of [...buildCommands, ...runCommands])
      expect(container.textContent, command).toContain(command);

    for (const preset of presets)
      expect(screen.getByText(preset.name), preset.name).toBeInTheDocument();
  });
});

describe('screenshots', () => {
  it('every shot a feature names exists on disk', () => {
    // A missing PNG is a 404 nobody sees until a reader does - `public/` is
    // fetched at runtime, unlike the generated JSON, which a build would refuse.
    // This is what stands in for that.
    const named = features.filter((f) => f.shot).map((f) => f.shot);

    expect(named.length).toBeGreaterThan(3);

    for (const shot of named)
      expect(existsSync(join('public', 'shots', `${shot ?? ''}.png`)), `${shot ?? ''}.png`).toBe(
        true,
      );
  });

  it('every shot on disk is one something names', () => {
    // The other direction: a PNG nothing references is 200KB of committed
    // binary nobody will ever look at again.
    const referenced = new Set([...features.map((f) => f.shot), 'gallery']);

    for (const file of readdirSync(join('public', 'shots')))
      expect(referenced, `${file} is committed and unreferenced`).toContain(
        file.replace('.png', ''),
      );
  });
});
