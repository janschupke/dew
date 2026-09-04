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
