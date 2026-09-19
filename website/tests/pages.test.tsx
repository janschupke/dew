import { existsSync, readdirSync } from 'node:fs';
import { join } from 'node:path';

import { render, screen } from '@testing-library/react';
import { describe, expect, it } from 'vitest';

import Features from '@/app/features/page';
import Home from '@/app/page';
import Mcp from '@/app/mcp/page';
import McpReference from '@/app/mcp/reference/page';
import McpResources from '@/app/mcp/resources/page';
import Score from '@/app/score/page';
import ScoreReference from '@/app/score/reference/page';
import Setup from '@/app/setup/page';
import Terms from '@/app/terms/page';
import { anchorForFeature, anchorForGroup, featureGroups, features } from '@/content/features';
import { checkCommands, platforms, presets } from '@/content/setup';

/*  Every page here is a plain synchronous component and stays one: Testing
    Library cannot render an async server component, so a page that reached for
    `await` would be a page nothing on this side could see.
*/
describe('pages', () => {
  it('the home page states what dew is, and that it is a prototype', () => {
    render(<Home />);

    expect(screen.getByRole('heading', { level: 1 })).toHaveTextContent('dew');
    expect(screen.getByText(/working prototype/i)).toBeInTheDocument();
  });

  it('the home page says it is open source and where the terms are', () => {
    const { container } = render(<Home />);

    expect(container.textContent).toMatch(/open source/i);
    expect(container.querySelector('a[href^="/terms"]')).not.toBeNull();
  });

  it('every page opens on the same rung', () => {
    // PageHeader is `pt-section` and carries no bottom padding, because what
    // follows normally brings its own top rung. Three pages used to hand-roll
    // a header at `pt-stack` instead, and the MCP page put a Band straight
    // after one - a Band's padding is INSIDE its border, so the rule landed on
    // the lead paragraph with no gap at all.
    for (const page of [
      <Home key="home" />,
      <Features key="features" />,
      <Score key="score" />,
      <Mcp key="mcp" />,
      <Setup key="setup" />,
      <Terms key="terms" />,
    ]) {
      const view = render(page);
      const heading = view.container.querySelector('h1');

      expect(heading?.parentElement?.className, heading?.textContent ?? '').toMatch(/\bpt-/);
      view.unmount();
    }
  });

  it('a sidebar page starts both its columns on the same rung', () => {
    // Toc used to put `pt-section` on the grid, and the first section of the
    // page carries one too - so the content column began a whole rung below
    // the sidebar. The rung is the aside's now, and the content column's first
    // block has to bring its own.
    for (const page of [
      <Features key="features" />,
      <Score key="score" />,
      <ScoreReference key="score-reference" />,
      <Mcp key="mcp" />,
      <McpReference key="mcp-reference" />,
      <McpResources key="mcp-resources" />,
    ]) {
      const view = render(page);
      const aside = view.container.querySelector('aside');
      const first = aside?.nextElementSibling?.firstElementChild;

      expect(aside?.className, page.key ?? '').toMatch(/\bpt-section\b/);
      expect(first?.className ?? '', page.key ?? '').toMatch(/\bpt-section\b/);
      view.unmount();
    }
  });

  it('the MCP page opens without a band against its lead', () => {
    // The reported defect, named: the element after the header must not be the
    // full-bleed band, whose top border would sit on the lead's baseline box.
    const { container } = render(<Mcp />);
    const header = container.querySelector('h1')?.parentElement;

    expect(header?.nextElementSibling?.className ?? '').not.toContain('border-y-hairline');
  });
});

describe('the features page', () => {
  it('renders every group and every feature in it', () => {
    const { container } = render(<Features />);

    expect(featureGroups.length).toBeGreaterThan(4);
    expect(features.length).toBeGreaterThan(15);

    for (const group of featureGroups) {
      const section = container.querySelector(`#${CSS.escape(anchorForGroup(group.id))}`);

      expect(section, `group ${group.id} has no section`).not.toBeNull();
      expect(section?.textContent).toContain(group.title);
    }

    for (const feature of features)
      expect(
        container.querySelector(`#${CSS.escape(anchorForFeature(feature.name))}`),
        feature.name,
      ).not.toBeNull();
  });

  it('every feature belongs to a group the page renders', () => {
    // The coverage half: a feature filed under a group id that no longer
    // exists would render nowhere and fail nothing.
    const ids = new Set(featureGroups.map((group) => group.id));

    for (const feature of features) expect(ids, feature.name).toContain(feature.group);
  });

  it('every group has a link in the sidebar', () => {
    const { container } = render(<Features />);

    const targets = new Set(
      [...container.querySelectorAll('[data-toc-group="groups"] a')].map((a) =>
        a.getAttribute('href'),
      ),
    );

    expect(targets.size).toBe(featureGroups.length);

    for (const group of featureGroups)
      expect(targets, group.id).toContain(`#${anchorForGroup(group.id)}`);
  });

  it('stacks the features without a picture in one column', () => {
    // The sidebar takes a column, so what is left is half the page - and the
    // features without a picture were a two-up grid of Cards inside THAT,
    // which is a bordered rectangle around a paragraph at a quarter of the
    // width. The picture blocks are the only two-column thing on the page.
    const { container } = render(<Features />);

    const lists = [...container.querySelectorAll('ul')];

    expect(lists.length).toBeGreaterThan(3);

    for (const list of lists) expect(list.className, list.className).not.toMatch(/grid-cols/);
  });

  it('a feature with a shot puts the sentence beside the picture', () => {
    // The section holds the prose block and the figure as its only two
    // children, in that order - a structure rather than a class.
    const { container } = render(<Features />);
    const withShot = features.filter((feature) => feature.shot !== undefined);

    expect(withShot.length).toBeGreaterThan(3);

    for (const feature of withShot) {
      const section = container.querySelector(`#${CSS.escape(anchorForFeature(feature.name))}`);
      const children = [...(section?.children ?? [])];

      expect(children, feature.name).toHaveLength(2);
      expect(children[0]?.querySelector('h3')?.textContent).toBe(feature.name);
      expect(children[1]?.tagName).toBe('FIGURE');
    }
  });

  it('no feature is left without a sentence', () => {
    for (const feature of features) expect(feature.body.length).toBeGreaterThan(40);
  });

  it('names the things the site never used to mention', () => {
    // The rewrite exists because wavetables, the FM matrix and SoundFont
    // playback were not on the site at all.
    const { container } = render(<Features />);

    for (const claim of ['wavetable', 'FM matrix', 'SF2', 'unison'])
      expect(container.textContent, claim).toContain(claim);
  });

  it('does not repeat the chain depth the code has outgrown', () => {
    // src/model/ProjectSchema.h: kMaxEffectsPerChain = 9. The site and the
    // README both said four for as long as they had said anything.
    const { container } = render(<Features />);

    expect(container.textContent).toContain('nine deep');
    expect(container.textContent).not.toMatch(/four deep/i);
  });
});

describe('the home page', () => {
  it('every group card goes somewhere the features page has', () => {
    const home = render(<Home />);

    // Matched on the FRAGMENT, not the whole href: next/link normalises the
    // trailing slash and `trailingSlash: true` is a next.config setting nothing
    // here applies.
    for (const group of featureGroups) {
      const card = home.container.querySelector(`a[href$="#${anchorForGroup(group.id)}"]`);

      expect(card, group.id).not.toBeNull();
      expect(card?.getAttribute('href')).toContain('/features');
      expect(card?.textContent).toContain(group.title);
    }

    home.unmount();

    const { container } = render(<Features />);

    for (const group of featureGroups)
      expect(container.querySelector(`#${anchorForGroup(group.id)}`), group.id).not.toBeNull();
  });

  it('no card without a destination answers the pointer', () => {
    // A card with nowhere to go does not answer the pointer at all.
    const { container } = render(<Home />);
    const flat = container.querySelectorAll('div.bg-surface');

    // It cannot pass by finding none.
    expect(flat.length).toBeGreaterThan(2);

    for (const card of flat) expect(card.className, card.textContent).not.toContain('hover:');
  });
});

describe('the setup page', () => {
  it('shows all three platforms, each complete on its own', () => {
    // A Linux reader must never have to read the macOS block to know what to
    // run. Every platform carries its own dependencies, build and run.
    const { container } = render(<Setup />);

    expect(platforms.map((platform) => platform.system).sort()).toEqual([
      'linux',
      'macos',
      'windows',
    ]);

    for (const platform of platforms) {
      const section = container.querySelector(`#${platform.system}`);

      expect(section, platform.system).not.toBeNull();
      expect(section?.textContent).toContain(platform.name);

      // textContent rather than getByText: the brew line carries the run of
      // spaces that lines its comment up in the README, and getByText collapses
      // whitespace - so it would pass against a reformatted command.
      for (const command of [...platform.build, ...platform.run])
        expect(section?.textContent, `${platform.system}: ${command}`).toContain(command);

      expect(platform.requirements.length).toBeGreaterThan(2);

      for (const requirement of platform.requirements)
        expect(section?.textContent, requirement.name).toContain(requirement.name);
    }
  });

  it('shows the gate and every preset', () => {
    const { container } = render(<Setup />);

    for (const command of checkCommands) expect(container.textContent).toContain(command);

    for (const preset of presets)
      expect(screen.getByText(preset.name), preset.name).toBeInTheDocument();
  });
});

describe('the terms page', () => {
  it('says what the licence allows and what it does not promise', () => {
    const { container } = render(<Terms />);
    const text = container.textContent;

    expect(text).toContain('GNU Affero General Public License');
    expect(text).toMatch(/without warranty of any kind/i);
    expect(text).toMatch(/as is/i);
    expect(text).toContain('JUCE');
  });

  it('does not call a copyleft licence permissive', () => {
    // The AGPL grants use for any purpose AND obliges an offer of source, so
    // "permissive" would be the wrong word for it.
    const { container } = render(<Terms />);

    expect(container.textContent).not.toMatch(/permissive/i);
    expect(container.textContent).toMatch(/corresponding source/i);
  });
});

describe('screenshots', () => {
  it('every shot a feature names exists on disk', () => {
    // A missing PNG is a 404 nobody sees until a reader does.
    const named = features.filter((f) => f.shot).map((f) => f.shot);

    expect(named.length).toBeGreaterThan(3);

    for (const shot of named)
      expect(existsSync(join('public', 'shots', `${shot ?? ''}.png`)), `${shot ?? ''}.png`).toBe(
        true,
      );
  });

  it('every shot on disk is one something names', () => {
    // The other direction: a PNG nothing references is committed binary nobody
    // will look at again.
    const referenced = new Set([...features.map((f) => f.shot), 'gallery']);

    for (const file of readdirSync(join('public', 'shots')))
      expect(referenced, `${file} is committed and unreferenced`).toContain(
        file.replace('.png', ''),
      );
  });
});
