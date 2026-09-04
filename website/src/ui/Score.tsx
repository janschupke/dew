import { sample, type Run, type Sample } from '@/lib/samples';

/*  The seven roles, in the editor's own colours.

    ScoreTokeniser::scheme maps each to a token: plain->textPrimary,
    keyword->accent, literal->playhead, string->success, comment->textDisabled,
    punctuation->textSecondary, invalid->danger. Written here as the same token
    names, so the mapping is visible in one place rather than being a fact you
    have to know.
*/
const roles: Record<string, string> = {
  plain: 'text-primary',
  keyword: 'text-accent',
  literal: 'text-playhead',
  string: 'text-success',
  comment: 'text-disabled',
  punctuation: 'text-secondary',
  invalid: 'text-danger',
};

/** The source split into runs and the gaps between them.
 *
 *  The gaps - whitespace - are emitted verbatim and unstyled, which is what
 *  makes the rendered text byte-identical to `source`. A test asserts exactly
 *  that, and it is the assertion that would catch a run with a wrong offset.
 */
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
    <figure className="my-xl">
      <pre className="bg-well-deep p-xl text-code-body overflow-x-auto rounded-md font-mono leading-relaxed">
        <code>
          {pieces(source, runs).map((piece, i) => (
            <span key={i} className={piece.role ? roles[piece.role] : undefined}>
              {piece.text}
            </span>
          ))}
        </code>
      </pre>

      {caption ? (
        <figcaption className="mt-sm text-small text-secondary">{caption}</figcaption>
      ) : null}
    </figure>
  );
}
