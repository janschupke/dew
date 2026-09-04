import raw from '@/generated/score-schema.json';

/** The score language, as dew_docs wrote it from lang::schema().
 *
 *  Typed here rather than imported as `any`: every page reads the schema
 *  through this module, so a shape change in the emitter is a type error in one
 *  place instead of a blank table in several.
 */
export interface KeySpec {
  readonly name: string;
  readonly kind: string;
  readonly kindDoc: string;
  readonly required: boolean;
  readonly overridable: boolean;
  readonly doc: string;
}

export interface BlockSpec {
  readonly kind: string;
  readonly doc: string;
  readonly topLevel: boolean;
  readonly keys: readonly KeySpec[];
  readonly children: readonly string[];
}

export interface ValueKind {
  readonly name: string;
  readonly doc: string;
  readonly members: readonly string[];
}

export interface Mode {
  readonly name: string;
  readonly degrees: readonly number[];
  readonly romanNumerals: boolean;
}

export interface Schema {
  readonly valueKinds: readonly ValueKind[];
  readonly blocks: readonly BlockSpec[];
  readonly modes: readonly Mode[];
}

export const schema: Schema = raw;

/** The anchor a block and a key are permanently addressable by.
 *
 *  `${block}-${key}` is injective because no BLOCK KIND contains a hyphen: the
 *  first hyphen therefore always separates the two halves, so two different
 *  pairs cannot mangle to one anchor. The same argument GenStrings.cmake makes
 *  for '.' -> '_', and it rests on the block half only - key names DO contain
 *  hyphens (parallel-fifths, voice-crossing, dissonance-on-strong). A test
 *  asserts it over the whole schema rather than trusting the sentence.
 *
 *  Permanent because the README, a diagnostic's help text and the app's Score
 *  tab can all link one, and a reference whose anchors move is a reference full
 *  of dead links.
 */
export const anchorForBlock = (kind: string): string => kind;
export const anchorForKey = (block: string, key: string): string => `${block}-${key}`;
export const anchorForValueKind = (name: string): string => `value-${name}`;

/** The kinds with a closed member list, which are the ones worth a table. */
export const closedKinds = schema.valueKinds.filter((kind) => kind.members.length > 0);
