import raw from '@/generated/score-samples.json';

/** Example scores, with the token runs that colour them — produced by the
 *  compiler's own tokeniser and the editor's own colour map. The site is a
 *  viewer, not a highlighter: there is no second grammar here. */
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
