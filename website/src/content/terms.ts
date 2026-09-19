/*  The legal page.
 *
 *  tests/terms.test.ts holds it against ../LICENSE and ../THIRD_PARTY.md, so a
 *  dependency added to the build cannot go unmentioned here.
 */
import { repositoryUrl } from '@/content/setup';

export interface TermsSection {
  readonly id: string;
  readonly title: string;
  readonly paragraphs: readonly string[];
}

/** The licence dew is under. Held against the text of ../LICENSE. */
export const licence = 'GNU Affero General Public License';
export const licenceShort = 'AGPLv3';
export const licenceUrl = `${repositoryUrl}/blob/master/LICENSE`;

export const terms: readonly TermsSection[] = [
  {
    id: 'licence',
    title: 'The licence',
    paragraphs: [
      'dew is free software, released under version 3 of the GNU Affero General Public License. The full text is in the repository as LICENSE, and it is the agreement; this page is only a summary, and the licence wins where the two disagree.',
      'You may run it for any purpose, private or commercial, on as many machines as you like. You may read the source, change it, and pass it on. There is nothing to register and nothing to pay.',
      'What the AGPL asks in return is source. If you give somebody a copy of dew — modified or not — you must offer them the corresponding source under the same licence. If you run a modified version somewhere other people interact with it over a network, you must offer them that source too. That network clause is the difference between the Affero licence and the ordinary GPL.',
      'The licence covers the program, not its output. A project file, a render or a score you write is yours, and dew claims nothing over any of them.',
    ],
  },
  {
    id: 'warranty',
    title: 'No warranty',
    paragraphs: [
      'dew is offered as is, without warranty of any kind, express or implied — including, but not limited to, the implied warranties of merchantability and fitness for a particular purpose. The entire risk as to its quality and performance is yours. Sections 15 and 16 of the licence are the binding wording.',
      'It is a working prototype. It can crash, and unsaved work goes with it. Keep your own copies.',
    ],
  },
  {
    id: 'juce',
    title: 'JUCE',
    paragraphs: [
      'dew is built on JUCE 9, which is dual-licensed: a commercial licence, or the AGPLv3. dew takes the AGPL terms, and inherits them.',
      'Both licence texts travel inside every build. dew-LICENSE.txt, JUCE-LICENSE.md and THIRD_PARTY.md sit in the application bundle’s Resources on macOS and beside the binary on Windows and Linux.',
    ],
  },
  {
    id: 'components',
    title: 'What else is inside it',
    paragraphs: [
      'Two libraries: JUCE, and Catch2 under the Boost Software License 1.0, which the test binaries link and no shipped artefact contains. THIRD_PARTY.md in the repository is generated at build time from what the build actually resolved, down to the version and the commit.',
      'LAME is neither bundled nor linked. MP3 export runs whatever lame is installed on your PATH as a separate process, under its own licence and on its own terms; without one, dew greys the option out.',
      'No typefaces and no sound files are redistributed. dew draws with the system typeface, its icons are drawn in code rather than shipped as images, and its wavetables are computed at load. A SoundFont stays your own file.',
    ],
  },
  {
    id: 'builds',
    title: 'Unsigned builds',
    paragraphs: [
      'None of the published builds is signed by a paid developer certificate, so macOS and Windows will each stop the first launch. Every release also carries SHA256SUMS.txt and a Sigstore build attestation, which records the repository, the workflow and the commit a file was built from. The download page says what each system does on first launch, and gives the two commands that check a download.',
    ],
  },
  {
    id: 'privacy',
    title: 'What dew collects',
    paragraphs: [
      'Nothing. There is no telemetry, no crash reporting, no update check and no account. dew makes no network requests of its own; the one address it ever opens is the source repository, handed to your browser when you click through from the About window.',
      'The MCP server is the only thing that listens. It ships switched off, and it binds the loopback interface only, so no machine but your own can reach it. It validates the Origin header, so a page you are merely visiting cannot drive it either, and each client is named to you and approved by you before it can do anything.',
      'This site sets no cookies and carries no analytics.',
    ],
  },
];
