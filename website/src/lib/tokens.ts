import raw from '@/generated/design-tokens.json';

/** The application's design tokens, generated from the palette it paints with.
 *
 *  Read by /design to show the vocabulary and by nothing else: every component
 *  takes its colours from the CSS custom properties generated from this file. */
export interface DesignTokens {
  readonly palette: string;
  readonly colour: Readonly<Record<string, string>>;
  readonly channelRamp: readonly string[];
  readonly lift: Readonly<Record<string, string>>;
  readonly emphasis: Readonly<Record<string, number>>;
  readonly space: Readonly<Record<string, number>>;
  readonly radius: Readonly<Record<string, number>>;
  readonly stroke: Readonly<Record<string, number>>;
  readonly type: Readonly<Record<string, number>>;
  readonly motion: Readonly<Record<string, number>>;
}

export const tokens: DesignTokens = raw;
