import en from '@/messages/en.json';

/** Every dotted path to a string leaf in the catalogue.
 *
 *  This is StringIds.h, and it needs no generator. cmake/GenStrings.cmake
 *  exists because C++ cannot turn a JSON file into a type at compile time -
 *  the walk, the mangling, the underscore ban that makes the mangling
 *  injective, the sorted enum, all of it is scaffolding around that one missing
 *  capability. TypeScript has the capability, so reproducing the SHAPE of the
 *  C++ solution here would be copying the workaround rather than the property.
 *
 *  `t('nav.reference')` compiles. `t('nav.referrence')` is a type error, in the
 *  editor, before any build - which the generated enum only manages after one.
 */
type Leaves<T, P extends string = ''> = {
  [K in keyof T & string]: T[K] extends string ? `${P}${K}` : Leaves<T[K], `${P}${K}.`>;
}[keyof T & string];

export type StringId = Leaves<typeof en>;

export type Args = Record<string, string | number>;

/** The ICU subset src/i18n/MessageFormat.cpp implements: named placeholders,
 *  and a plural with `#` standing for the count.
 *
 *  Deliberately the same subset rather than a larger one. A message the app
 *  could not express is a message that cannot move between the two catalogues,
 *  and the point of matching their shape is that a key can.
 */
function format(raw: string, args: Args): string {
  const plural = /\{(\w+),\s*plural,\s*(.+?)\}\s*$/s;

  const withPlurals = raw.replace(plural, (whole, name: string, branches: string) => {
    const count = args[name];

    if (typeof count !== 'number') return whole;

    // `one` and `other` only. English has no `few`, and inventing categories a
    // locale does not use is how a plural rule starts lying.
    const wanted = count === 1 ? 'one' : 'other';
    const found = new RegExp(`${wanted}\\s*\\{([^}]*)\\}`).exec(branches);

    return found?.[1]?.replace(/#/g, String(count)) ?? whole;
  });

  return withPlurals.replace(/\{(\w+)\}/g, (whole, name: string) => {
    const value = args[name];
    return value === undefined ? whole : String(value);
  });
}

/** NEVER empty.
 *
 *  A key the catalogue does not hold returns its own dotted path, visible on
 *  the page and impossible to mistake for prose - the same rule
 *  src/i18n/Strings.h follows so a gap shows up in a screenshot rather than as
 *  a blank panel nobody notices. The type says this branch is unreachable; it
 *  is kept because `next dev` does not typecheck.
 */
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
