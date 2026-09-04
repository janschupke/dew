import type { NextConfig } from 'next';

/*  Static export, and that is the whole point of the generated JSON.

    Every input this site has is a committed file: the score schema, the design
    tokens and the screenshots. Nothing is fetched, nothing is authenticated and
    nothing is rendered per request - so there is no job for a server to do, and
    `out/` is a directory anybody can serve. It is also what lets CI check the
    site on a Linux runner in a minute rather than behind a JUCE build.

    images.unoptimized follows from it: the optimiser IS a server. The shots are
    served as dew_shot wrote them, which is why scripts/gen-shots.sh renders them
    at the size they are shown at.

    typescript.ignoreBuildErrors is stated rather than left to its default,
    because the gate depends on it: a build that quietly stopped typechecking
    would make `npm run check` a slower way of running eslint. There is no
    eslint key - Next 16 removed `next lint`, so eslint is its own step in
    package.json rather than something the build runs.
*/
const config: NextConfig = {
  output: 'export',
  trailingSlash: true,
  images: { unoptimized: true },
  typescript: { ignoreBuildErrors: false },
};

export default config;
