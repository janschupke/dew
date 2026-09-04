import { render } from '@testing-library/react';
import { describe, expect, it } from 'vitest';

import McpReference from '@/app/mcp/reference/page';
import Mcp from '@/app/mcp/page';
import { anchorForGuide, anchorForTool, mcp, readTools, writeTools } from '@/lib/mcp';

const argumentCount = mcp.tools.reduce((n, tool) => n + tool.args.length, 0);

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

  it('every tool has a link in the page nav', () => {
    // The lesson the score reference already learned: a gate over the sections
    // is not a gate over the list of them, and the first attempt at one shipped
    // an index that silently omitted a block.
    const { container } = render(<McpReference />);
    const nav = container.querySelector('nav');

    expect(nav).not.toBeNull();

    const targets = new Set(
      [...(nav?.querySelectorAll('a') ?? [])].map((a) => a.getAttribute('href')),
    );

    expect(targets.size).toBe(mcp.tools.length);

    for (const tool of mcp.tools) expect(targets).toContain(`#${anchorForTool(tool.name)}`);
  });

  it('renders every guide page, whole', () => {
    // These ship as MCP resources, so the page and the resource are the same
    // words. A paragraph that reached one and not the other would be advice a
    // client acts on and a reader cannot check.
    const { container } = render(<McpReference />);

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
      expect(hrefs).toContain(`/mcp/reference#${anchorForGuide(section.id)}`);
  });
});
