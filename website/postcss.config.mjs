/*  Tailwind v4 is a PostCSS plugin and needs no config file of its own: the
    theme is CSS, in src/app/theme.generated.css, which is what stops the
    palette having a second home. See scripts/gen-theme.mjs.
*/
export default { plugins: { '@tailwindcss/postcss': {} } };
