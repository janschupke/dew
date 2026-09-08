import { sample, type Run, type Sample } from '@/lib/samples';

/*  The seven roles, in the editor's own colours and by the editor's own token
    names — the site has no highlighter of its own, and paints spans over text
    the compiler already tokenised. */
const roles: Record<string, string> = {
  plain: 'text-primary',
  keyword: 'text-accent',
  literal: 'text-playhead',
  string: 'text-success',
  comment: 'text-disabled',
  punctuation: 'text-secondary',
  invalid: 'text-danger',
};

/** The source split into runs and the gaps between them. The gaps are emitted
 *  verbatim, which is what keeps the rendered text byte-identical to `source` —
 *  the assertion that catches a run with a wrong offset. */
function pieces(source: string, runs: readonly Run[]): { text: string; role: string | null }[] {
  const out: { text: string; role: string | null }[] = [];
  let at = 0;

  for (const run of runs) {
    if (run.offset > at) out.push({ text: source.slice(at, run.offset), role: null });

    out.push({ text: source.slice(run.offset, run.offset + run.length), role: run.role });
    at = run.offset + run.length;
  }

  if (at < source.length) out.push({ text: source.slice(at), role: null });

  return out;
}

export function Score({
  name,
  lines,
  caption,
}: {
  name: string;
  lines?: readonly [number, number];
  caption?: string;
}) {
  const whole: Sample = sample(name);
  const source = lines
    ? whole.source
        .split('\n')
        .slice(lines[0] - 1, lines[1])
        .join('\n')
    : whole.source;

  // Slicing by line means slicing the runs too. Offsets are recomputed against
  // the excerpt rather than filtered, so an excerpt colours exactly as the
  // whole file does at those lines.
  const start = lines
    ? whole.source
        .split('\n')
        .slice(0, lines[0] - 1)
        .join('\n').length + (lines[0] > 1 ? 1 : 0)
    : 0;

  const runs = whole.runs
    .filter((r) => r.offset >= start && r.offset + r.length <= start + source.length)
    .map((r) => ({ ...r, offset: r.offset - start }));

  return (
    <figure className="my-stack">
      <pre className="bg-well-deep p-stack text-code-body overflow-x-auto rounded-md font-mono leading-relaxed">
        <code>
          {pieces(source, runs).map((piece, i) => (
            <span key={i} className={piece.role ? roles[piece.role] : undefined}>
              {piece.text}
            </span>
          ))}
        </code>
      </pre>

      {caption ? (
        <figcaption className="mt-sm text-fine text-secondary">{caption}</figcaption>
      ) : null}
    </figure>
  );
}
