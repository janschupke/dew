import { render } from '@testing-library/react';
import { describe, expect, it } from 'vitest';

import McpReference from '@/app/mcp/reference/page';
import McpResources from '@/app/mcp/resources/page';
import Mcp from '@/app/mcp/page';
import { anchorForGuide, anchorForTool, mcp, readTools, writeTools } from '@/lib/mcp';

const argumentCount = mcp.tools.reduce((n, tool) => n + tool.args.length, 0);

/** Every fragment one sidebar group links to. */
const anchorsIn = (container: HTMLElement, group: string): Set<string> =>
  new Set(
    [...container.querySelectorAll(`[data-toc-group="${group}"] a`)].map(
      (a) => a.getAttribute('href') ?? '',
    ),
  );

describe('the generated MCP reference', () => {
  // Control case. Every assertion below walks the table, and a walk over an
  // empty one passes while proving nothing.
  it('has a table to be a gate over', () => {
    expect(mcp.tools.length).toBeGreaterThan(25);
    expect(argumentCount).toBeGreaterThan(50);
    expect(mcp.guide.length).toBeGreaterThanOrEqual(5);
    expect(mcp.protocolVersion).toMatch(/^\d{4}-\d{2}-\d{2}$/);
  });

  it('splits the tools into the ones that read and the ones that change', () => {
    // The first thing somebody deciding what to allow wants to see, and the
    // whole of dew's permission model - so a tool in neither group, or in both,
    // is a tool whose scope the page has stopped reading.
    expect(readTools.length + writeTools.length).toBe(mcp.tools.length);
    expect(readTools.length).toBeGreaterThan(5);
    expect(writeTools.length).toBeGreaterThan(15);
  });

  it('renders every tool, with its summary and its arguments', () => {
    const { container } = render(<McpReference />);

    for (const tool of mcp.tools) {
      const section = container.querySelector(`#${CSS.escape(anchorForTool(tool.name))}`);

      expect(section, `tool ${tool.name} has no section`).not.toBeNull();
      expect(section?.textContent).toContain(tool.name);
      expect(section?.textContent).toContain(tool.summary.replaceAll('`', ''));

      for (const arg of tool.args)
        expect(section?.textContent, `${tool.name} does not show ${arg.name}`).toContain(arg.name);
    }
  });

  it('renders a nested argument as the path a caller writes', () => {
    // Almost every write is batch-first, so the argument that matters is inside
    // an array. A page that stopped at the array would document a blob.
    const { container } = render(<McpReference />);
    const section = container.querySelector(`#${CSS.escape(anchorForTool('params_write'))}`);

    expect(section?.textContent).toContain('entries[].param');
    expect(section?.textContent).toContain('entries[].value');
  });

  it('every tool has a link in the sidebar, in the group its scope puts it in', () => {
    // The lesson the score reference already learned: a gate over the sections
    // is not a gate over the list of them, and the first attempt at one shipped
    // an index that silently omitted a block.
    //
    // Asked per GROUP, not over the whole nav. The sidebar's whole claim is
    // that a reader can see which tools change their project before reading a
    // word, and a count over both groups together would pass with every tool
    // filed under the wrong one.
    const { container } = render(<McpReference />);

    expect(container.querySelector('nav')).not.toBeNull();

    for (const [group, tools] of [
      ['read', readTools],
      ['write', writeTools],
    ] as const) {
      const targets = anchorsIn(container, group);

      expect(targets.size, group).toBe(tools.length);

      for (const tool of tools)
        expect(targets, `${tool.name} has no sidebar link`).toContain(
          `#${anchorForTool(tool.name)}`,
        );
    }
  });

  it('the sidebar names nothing the page does not carry', () => {
    // The other direction, which is the half a link check cannot do: an entry
    // pointing at an id nothing renders is a dead anchor, and the browser
    // reports one only to whoever clicked it.
    for (const page of [<McpReference key="reference" />, <McpResources key="resources" />]) {
      const { container } = render(page);

      const links = [...(container.querySelector('nav')?.querySelectorAll('a') ?? [])];

      expect(links.length).toBeGreaterThan(4);

      for (const link of links) {
        const href = link.getAttribute('href') ?? '';

        expect(href, 'a sidebar entry that is not a fragment').toMatch(/^#/);
        expect(
          container.querySelector(`#${CSS.escape(href.slice(1))}`),
          `${href} is in the sidebar and not on the page`,
        ).not.toBeNull();
      }
    }
  });

  it('renders every guide page, whole', () => {
    // These ship as MCP resources, so the page and the resource are the same
    // words. A paragraph that reached one and not the other would be advice a
    // client acts on and a reader cannot check.
    //
    // They are a page of their own now. They used to sit between the tool index
    // and the tools, which put five documents nobody had come for in front of
    // the table everybody had.
    const { container } = render(<McpResources />);

    for (const section of mcp.guide) {
      const element = container.querySelector(`#${CSS.escape(anchorForGuide(section.id))}`);

      expect(element, `guide ${section.id} has no section`).not.toBeNull();
      expect(element?.textContent).toContain(section.title);
      expect(element?.textContent).toContain(`dew://guide/${section.id}`);

      for (const paragraph of section.paragraphs)
        expect(element?.textContent).toContain(paragraph.replaceAll('`', ''));
    }
  });

  it('anchors are unique across tools and guide pages', () => {
    const anchors = [
      ...mcp.tools.map((tool) => anchorForTool(tool.name)),
      ...mcp.guide.map((section) => anchorForGuide(section.id)),
    ];

    expect(new Set(anchors).size).toBe(anchors.length);
  });
});

describe('the MCP page', () => {
  it('links every guide page and the reference', () => {
    const { container } = render(<Mcp />);

    // Compared without the trailing slash: next/link normalises `/a/#b` to
    // `/a#b`, so asserting the string as written would be asserting Next's
    // formatting rather than the page's links.
    const hrefs = [...container.querySelectorAll('a')].map((a) =>
      (a.getAttribute('href') ?? '').replace('/#', '#'),
    );

    expect(hrefs.some((href) => href.startsWith('/mcp/reference'))).toBe(true);

    for (const section of mcp.guide)
      expect(hrefs).toContain(`/mcp/resources#${anchorForGuide(section.id)}`);
  });
});
