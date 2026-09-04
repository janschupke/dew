import js from '@eslint/js';
import ts from 'typescript-eslint';
import next from '@next/eslint-plugin-next';
import hooks from 'eslint-plugin-react-hooks';
import a11y from 'eslint-plugin-jsx-a11y';
import prettier from 'eslint-config-prettier';

export default ts.config(
  {
    ignores: [
      '.next/**',
      'out/**',
      // Written by dew_docs, dew_shot and gen-theme.mjs, and held byte for byte
      // by gates elsewhere. Linting them would be the second opinion.
      'src/generated/**',
      'next-env.d.ts',
    ],
  },
  js.configs.recommended,
  ...ts.configs.strictTypeChecked,
  ...ts.configs.stylisticTypeChecked,
  {
    files: ['**/*.ts', '**/*.tsx'],
    languageOptions: { parserOptions: { projectService: true } },
  },
  {
    plugins: { '@next/next': next, 'react-hooks': hooks, 'jsx-a11y': a11y },
    rules: {
      ...next.configs.recommended.rules,
      ...next.configs['core-web-vitals'].rules,
      ...a11y.configs.strict.rules,
    },
  },
  {
    // "Nothing resolves a string BY its path" - src/i18n/Strings.h. Without
    // this somebody builds a key out of schema data at runtime and the type
    // check on StringId becomes decorative.
    //
    // src/ only. The catalogue test deliberately answers EVERY message, which
    // it can only do by computing the id - that is the one place a dynamic key
    // is the point rather than the defect.
    files: ['src/**/*.ts', 'src/**/*.tsx'],
    rules: {
      'no-restricted-syntax': [
        'error',
        {
          selector: "CallExpression[callee.name='t'] > :first-child:not(Literal)",
          message:
            't() takes a literal key. Building one at runtime is a way to name a key the type does not have.',
        },
      ],
    },
  },
  {
    // The build scripts and the flat configs are plain ESM. They are outside
    // tsconfig's include on purpose - a config file is not part of the site -
    // so the type-aware rules have no program for them and must be off.
    files: ['**/*.mjs'],
    ...ts.configs.disableTypeChecked,
    languageOptions: {
      parserOptions: { projectService: false, project: false },
      globals: { process: 'readonly', console: 'readonly' },
    },
  },
  prettier,
);
