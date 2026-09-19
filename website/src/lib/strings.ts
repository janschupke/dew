import en from '@/messages/en.json';

/** Every dotted path to a string leaf in the catalogue, as a type.
 *
 *  `t('nav.reference')` compiles; `t('nav.referrence')` is a type error, in the
 *  editor, before any build. */
type Leaves<T, P extends string = ''> = {
  [K in keyof T & string]: T[K] extends string ? `${P}${K}` : Leaves<T[K], `${P}${K}.`>;
}[keyof T & string];

export type StringId = Leaves<typeof en>;

export type Args = Record<string, string | number>;

/** The ICU subset the application implements: named placeholders, and a plural
 *  with `#` standing for the count. */
function format(raw: string, args: Args): string {
  const plural = /\{(\w+),\s*plural,\s*(.+?)\}\s*$/s;

  const withPlurals = raw.replace(plural, (whole, name: string, branches: string) => {
    const count = args[name];

    if (typeof count !== 'number') return whole;

    // `one` and `other` only. English has no `few`.
    const wanted = count === 1 ? 'one' : 'other';
    const found = new RegExp(`${wanted}\\s*\\{([^}]*)\\}`).exec(branches);

    return found?.[1]?.replace(/#/g, String(count)) ?? whole;
  });

  return withPlurals.replace(/\{(\w+)\}/g, (whole, name: string) => {
    const value = args[name];
    return value === undefined ? whole : String(value);
  });
}

/** NEVER empty. A key the catalogue does not hold returns its own dotted path,
 *  visible on the page and impossible to mistake for prose. The type says that
 *  branch is unreachable; it is kept because `next dev` does not typecheck. */
export function t(id: StringId, args?: Args): string {
  const raw = id
    .split('.')
    .reduce<unknown>(
      (node, segment) =>
        typeof node === 'object' && node !== null
          ? (node as Record<string, unknown>)[segment]
          : undefined,
      en,
    );

  if (typeof raw !== 'string') return id;

  return args ? format(raw, args) : raw;
}
