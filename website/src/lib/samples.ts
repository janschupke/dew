import raw from '@/generated/score-samples.json';

/** Example scores, with the token runs that colour them.
 *
 *  Written by `dew_shot samples`, which scans with the compiler's own
 *  lang::tokenize and classifies with the editor's own
 *  ScoreTokeniser::colourFor. So this site is a VIEWER, not a highlighter:
 *  there is no second grammar here, which is what
 *  .ai/rules/score-language.md requires and what a TypeScript tokenizer could
 *  only have approximated.
 */
export interface Run {
  readonly offset: number;
  readonly length: number;
  readonly role: string;
}

export interface Sample {
  readonly name: string;
  readonly source: string;
  readonly runs: readonly Run[];
}

export const samples: readonly Sample[] = raw;

export function sample(name: string): Sample {
  const found = samples.find((s) => s.name === name);

  if (!found) throw new Error(`no score sample named ${name}`);

  return found;
}
