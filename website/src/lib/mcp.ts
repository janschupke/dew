import raw from '@/generated/mcp-tools.json';

/** dew's MCP surface, as dew_mcp wrote it from control::ops() and
 *  control::guide().
 *
 *  Typed here rather than imported as `any`, for the reason schema.ts is: every
 *  page reads it through this module, so a shape change in the emitter is a type
 *  error in one place instead of a blank table in several.
 */
export interface ArgSpec {
  readonly name: string;
  readonly kind: string;
  readonly required: boolean;
  readonly doc: string;
  /** True when `fields` describes the ELEMENT of an array rather than an
   *  object's properties - the convention that lets `ids` be a list of integers
   *  instead of a list of unconstrained anythings. */
  readonly holdsBareElements: boolean;
  readonly fields: readonly ArgSpec[];
}

export interface Tool {
  readonly name: string;
  /** 'read' or 'write'. The whole of dew's permission model: a client approved
   *  for reading is refused every tool whose scope is 'write'. */
  readonly scope: 'read' | 'write';
  readonly summary: string;
  readonly doc: string;
  readonly args: readonly ArgSpec[];
}

export interface GuideSection {
  readonly id: string;
  readonly title: string;
  readonly summary: string;
  readonly paragraphs: readonly string[];
}

export interface McpReference {
  readonly protocolVersion: string;
  readonly guide: readonly GuideSection[];
  readonly tools: readonly Tool[];
}

export const mcp: McpReference = raw as McpReference;

/** The anchor a tool and a guide page are permanently addressable by.
 *
 *  Permanent because a tool's own documentation links another's, the guide
 *  resources name each other, and a reference whose anchors move is a reference
 *  full of dead links. The same argument schema.ts makes for the score
 *  language's.
 *
 *  Injective without needing an argument about separators: operation names and
 *  guide ids are both drawn from a single flat namespace of snake_case words,
 *  and the two are prefixed apart. A test asserts it over the whole file rather
 *  than trusting this sentence.
 */
export const anchorForTool = (name: string): string => `tool-${name}`;
export const anchorForGuide = (id: string): string => `guide-${id}`;

/** Tools that only read, and tools that change the project. The page groups by
 *  this because it is the first thing somebody deciding what to allow wants to
 *  see. */
export const readTools = mcp.tools.filter((tool) => tool.scope === 'read');
export const writeTools = mcp.tools.filter((tool) => tool.scope === 'write');

/** An argument's type as the page prints it: the kind, plus what an array
 *  holds. `any` is a real answer - a parameter's value is whatever that
 *  parameter takes - and is printed rather than hidden. */
export function describeKind(arg: ArgSpec): string {
  if (arg.kind !== 'array') return arg.kind;
  if (arg.holdsBareElements && arg.fields[0]) return `array of ${arg.fields[0].kind}`;
  if (arg.fields.length > 0) return 'array of objects';

  return 'array';
}
