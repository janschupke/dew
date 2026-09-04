import { readdirSync, readFileSync, statSync } from 'node:fs';
import { join } from 'node:path';

/** Every source file under `src/`, the way the C++ gates walk DEW_SOURCE_DIR.
 *
 *  `src/generated` is excluded: it is written by dew_docs and dew_shot, and a
 *  gate that judged an emitter's output by the rules for hand-written code
 *  would be judging the wrong thing.
 */
export function sourceFiles(root = 'src'): string[] {
  const found: string[] = [];

  const walk = (dir: string) => {
    for (const entry of readdirSync(dir)) {
      const path = join(dir, entry);

      if (statSync(path).isDirectory()) {
        if (entry !== 'generated') walk(path);
        continue;
      }

      if (/\.(ts|tsx|css)$/.test(entry) && !entry.endsWith('.generated.css')) found.push(path);
    }
  };

  walk(root);

  return found.sort();
}

/** A file with its comments blanked out, LINE STRUCTURE INTACT.
 *
 *  Every comment character becomes a space and every newline survives, so a
 *  match still reports the line it is really on. A stripper that deleted the
 *  spans instead would renumber every line after the first block comment.
 *
 *  Line-leading markers are not enough, which is not a hypothetical: the first
 *  version of this matched `^\s*(//|\*)` and the continuation lines of a CSS
 *  block comment - which start with ordinary prose - sailed straight past it.
 *  A gate reporting its own explanation of itself is a gate somebody deletes.
 *
 *  String literals are deliberately NOT stripped. In TSX an offending colour
 *  would BE inside a string, so blanking those would blind every gate here.
 */
export function withoutComments(source: string): string {
  let out = '';
  let i = 0;

  const blank = (text: string) => text.replace(/[^\n]/g, ' ');

  while (i < source.length) {
    const block = source.indexOf('/*', i);
    const line = source.indexOf('//', i);

    const next = block < 0 ? line : line < 0 ? block : Math.min(block, line);

    if (next < 0) {
      out += source.slice(i);
      break;
    }

    out += source.slice(i, next);

    if (next === block) {
      const end = source.indexOf('*/', next + 2);
      const stop = end < 0 ? source.length : end + 2;

      out += blank(source.slice(next, stop));
      i = stop;
    } else {
      const end = source.indexOf('\n', next);
      const stop = end < 0 ? source.length : end;

      out += blank(source.slice(next, stop));
      i = stop;
    }
  }

  return out;
}

export interface Offence {
  readonly file: string;
  readonly line: number;
  readonly text: string;
}

/** Lines matching `pattern`, reported with where they are.
 *
 *  Reports the file and the line rather than a count, because a gate that says
 *  only "something is wrong" is one somebody turns off.
 */
export function offenders(pattern: RegExp, files = sourceFiles()): Offence[] {
  const found: Offence[] = [];

  for (const file of files) {
    const raw = readFileSync(file, 'utf8');
    const code = withoutComments(raw).split('\n');
    const original = raw.split('\n');

    code.forEach((text, i) => {
      if (pattern.test(text)) found.push({ file, line: i + 1, text: (original[i] ?? '').trim() });
    });
  }

  return found;
}

export const report = (found: Offence[]): string =>
  found.map((o) => `${o.file}:${String(o.line)}  ${o.text}`).join('\n');

/** True when a Tailwind arbitrary value encodes something the design system
 *  should have owned.
 *
 *  Exported so the gate and its control case share ONE implementation. Two
 *  copies of a predicate is two things that can disagree, and the one that
 *  disagrees silently is the gate.
 *
 *  Refused: a colour literal, and a length in px/rem/em. Those are exactly what
 *  `tokens::colour`, `tokens::space`, `tokens::radius` and `tokens::type`
 *  already name, so writing one in a bracket is a second copy of a rung.
 *
 *  Allowed, and each for a stated reason:
 *   - a custom property (`duration-[--motion-quick-ms]`) is the token, resolved
 *     at runtime rather than at build time;
 *   - `fr` and bare keywords (`grid-cols-[auto_1fr]`) are a grid template, and
 *     there is no rung for one;
 *   - `72rem` is the page width and `68ch` the measure. A text column is
 *     counted in characters, which is not something Tokens.h has an opinion
 *     about - the application has no prose.
 */
export function unownedMeasurement(value: string): boolean {
  if (value.includes('--') || value.includes('var(')) return false;
  if (value === '68ch' || value === '72rem') return false;

  return /#[0-9a-fA-F]{3,8}/.test(value) || /\d+(?:\.\d+)?(?:px|rem|em)\b/.test(value);
}

export const arbitraryValues = (text: string): string[] =>
  [...text.matchAll(/-\[([^\]]+)\]/g)].map((m) => m[1] ?? '');
