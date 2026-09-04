import raw from '@/generated/design-tokens.json';

/** dew::tokens, as `dew_shot tokens` wrote them from darkPalette().
 *
 *  Read by the /design page to show the vocabulary, and by nothing else - every
 *  component takes its colours from the CSS custom properties that
 *  scripts/gen-theme.mjs generates from this same file. A component reading a
 *  hex out of here would be the one place a colour could be written by hand.
 */
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
