/** What dew does, for a reader deciding whether it is interesting.
 *
 *  Grouped by what you are trying to do, not by which tab it happens in: the
 *  channel rack and the piano roll are one job, and the instrument panel that
 *  sits beside both of them is another.
 */
import type { ShotName } from '@/ui/Shot';

export type FeatureGroupId = 'sound' | 'notes' | 'arranging' | 'mixing' | 'working' | 'writing';

export interface FeatureGroup {
  readonly id: FeatureGroupId;
  readonly title: string;
  readonly body: string;
}

export interface Feature {
  readonly group: FeatureGroupId;
  readonly name: string;
  readonly body: string;

  /** A name from `shotSizes`, not any string: a shot the site names and
   *  `public/shots/` does not hold is a broken image. */
  readonly shot?: ShotName;

  /** Where the rest of it is written up, when there is a page for it. The
   *  label is here rather than a message key, because `t()` takes a literal
   *  and one chosen per feature would have to be built. */
  readonly href?: string;
  readonly hrefLabel?: string;
}

/** The id a group's section carries, and the fragment the home page links to. */
export const anchorForGroup = (id: FeatureGroupId): string => `group-${id}`;

/** The id a feature's section carries. Derived once, so a name typed on one
 *  page and slugged on another cannot drift. */
export const anchorForFeature = (name: string): string =>
  name.toLowerCase().replace(/[^a-z0-9]+/g, '-');

export const featureGroups: readonly FeatureGroup[] = [
  {
    id: 'sound',
    title: 'Sound',
    body: 'Three oscillators on every channel, and everything that drives them.',
  },
  {
    id: 'notes',
    title: 'Writing notes',
    body: 'Two views of the same notes, and the metre they sit in.',
  },
  {
    id: 'arranging',
    title: 'Arranging',
    body: 'Patterns as clips on a timeline, and curves that move things while it plays.',
  },
  {
    id: 'mixing',
    title: 'Mixing',
    body: 'Inserts, effect chains, and what you can see of the signal.',
  },
  {
    id: 'working',
    title: 'Working with it',
    body: 'Getting audio in, getting a file out, and coming back to where you were.',
  },
  {
    id: 'writing',
    title: 'Two other ways to write a song',
    body: 'The score language, and the MCP server an agent connects to.',
  },
];

export const features: readonly Feature[] = [
  {
    group: 'sound',
    name: 'Oscillators',
    body: 'Three slots on every channel, each running a band-limited sine, saw, square or triangle with its own octave, detune, gain and on/off switch. Sixteen voices deep, on up to sixty-four channels.',
  },
  {
    group: 'sound',
    name: 'Wavetables',
    body: 'Any slot can run a wavetable instead: five factory tables of sixteen morph frames each, band-limited and built at load rather than shipped as files, with up to seven detuned unison voices. The morph position is a knob, and the envelope or an LFO can move it.',
  },
  {
    group: 'sound',
    name: 'FM matrix',
    body: 'A fourth segment on the oscillator selector: a full three-by-three matrix routing each oscillator into the others, its own diagonal included, with a per-slot output column. The default is the identity, so the three simply sum until you say otherwise.',
  },
  {
    group: 'sound',
    name: 'SoundFonts',
    body: 'A channel can play an SF2 file instead of its oscillators — key and velocity zones, root keys, tuning, pan, loop modes and the font’s own volume and modulation envelopes. Six offsets sit on top, so nothing overwrites what the font’s author decided.',
  },
  {
    group: 'sound',
    name: 'Envelope and LFOs',
    body: 'One ADSR behind the three slots, and an LFO on each of them. A shape, a rate in hertz or locked to the tempo, and three independent depths — to pitch, to level and to pan — so any combination of the three moves and the ones left at zero do not.',
  },
  {
    group: 'sound',
    name: 'Presets',
    body: 'Forty factory sounds in seven categories: three for each effect type, six for the synth, two for an audio channel and two for a soundfont. A preset carries the sound, and not a channel’s name, colour, routing, level or effect chain, so loading one mid-mix cannot move a fader or retune a part you have already written.',
  },

  {
    group: 'notes',
    name: 'Channel rack',
    body: 'A step grid, one row per channel. Click or drag to write steps. Volume, pan and an on/off indicator sit on the row itself, so a pattern is balanced where it is written.',
    shot: 'channel-rack',
  },
  {
    group: 'notes',
    name: 'Piano roll',
    body: 'The same notes as the step grid, in a second view. Rubber-band select, move a chord without losing its shape, drag velocity bars in the lane below, click the keys to hear them. Select, paint and slice tools, a snap grid from 1/16 to a bar, and quantize, transpose and randomize over the selection or the whole channel.',
    shot: 'piano-roll',
  },
  {
    group: 'notes',
    name: 'Time signature',
    body: '3/4, 5/4, 6/8, 7/8 and the rest, driving bar lines, beat shading, the bars:beats:ticks readout, what “Bar” snaps to and exported MIDI. A step is the same length in 3/4 as in 4/4.',
  },

  {
    group: 'arranging',
    name: 'Playlist',
    body: 'Pattern clips on lanes along a bar timeline; a clip longer than its pattern repeats it. Drag clips between lanes, double-click to open a pattern, switch a lane off. Lanes stretch, for a curve that needs the height.',
    shot: 'playlist',
  },
  {
    group: 'arranging',
    name: 'Automation',
    body: 'Clips on the playlist driving nine scopes: channel and track volume and pan, master gain, the tempo, and any parameter of any oscillator, envelope, soundfont or effect. Reached from the right-click menu of the knob itself. Drag points, bend the segment between two of them, or make it a step.',
  },
  {
    group: 'arranging',
    name: 'Loop and transport',
    body: 'Play, stop, tempo, metre, a pattern-or-song switch, and pattern add, duplicate and delete with an editable length. A span selected on any ruler is the span that plays, and a loop drawn ahead of the playhead is played into rather than jumped to.',
  },

  {
    group: 'mixing',
    name: 'Mixer',
    body: 'A fader, pan, an on/off indicator and a peak meter per insert, plus master — twenty inserts in a new project and up to thirty-two. Each strip lists the channels routed into it, and clicking one goes there.',
    shot: 'mixer',
  },
  {
    group: 'mixing',
    name: 'Effects',
    body: 'Ten of them — filter, reverb, delay, drive, distortion, chorus, phaser, a 3-band EQ, a compressor and a limiter — chained up to nine deep on any channel, mixer insert or the master, each with its own dry/wet. Drag a card by its grip to reorder: the whole drag is one undo step, and Escape abandons it.',
  },
  {
    group: 'mixing',
    name: 'Metering',
    body: 'A peak meter on every strip, and an oscilloscope and spectrum of the master output on the transport bar, triggered on a rising zero crossing so the trace stands still.',
  },

  {
    group: 'working',
    name: 'Recording',
    body: 'An audio channel plays a recording instead of its oscillators. Arm with R, press record, and the take lands on the playlist at the bar the playhead was on, with a count-in that starts the file on the downbeat. The microphone is asked for when you arm a channel, never at startup.',
  },
  {
    group: 'working',
    name: 'Render and export',
    body: 'The song, one pattern or a span of bars, as WAV, FLAC, MP3 or MIDI, with sample rate, bit depth, release tail, normalize, fades, dither and stems. A bar range renders from the start and throws the head away, so reverb tails and automation arrive at the in-point in the state playing there would leave them.',
  },
  {
    group: 'working',
    name: 'Audio and MIDI devices',
    body: 'Driver, output, input, input channels, sample rate and buffer size, with the resulting latency, an input meter and a test tone. MIDI in carries velocity, pitch bend, the mod wheel and the sustain pedal, and a controller survives being unplugged and put back.',
  },
  {
    group: 'working',
    name: 'Files and undo',
    body: 'New, open, save, save as, dirty tracking and a save-before-closing prompt, with undo and redo over every edit — a whole drag, a preset load and a batch written by an agent each being one step.',
  },
  {
    group: 'working',
    name: 'Session state',
    body: 'Window geometry, interface scale, active tab, selections, the piano roll’s zoom, scroll, row height and snap, the playlist’s lane height and the score’s text size all come back as you left them. The piano roll’s tool does not.',
  },

  {
    group: 'writing',
    name: 'Score',
    body: 'A fifth tab holding the whole song as text: key, metre, chord progression, sections, and per channel a voicing, a melody or a counterpoint answering another voice. Checked as you type and compiled to real patterns and clips on demand. The text lives in the project, so recompiling leaves anything you have edited by hand alone.',
    shot: 'score',
    href: '/score/',
    hrefLabel: 'The score language',
  },
  {
    group: 'writing',
    name: 'Agent control',
    body: 'dew runs a Model Context Protocol server inside the application, so a coding agent can read and change the project you have open. It is off until you turn it on, listens on the loopback interface only, and names each client and asks before letting it in. Every call that changes the project is exactly one undo step.',
    href: '/mcp/',
    hrefLabel: 'Control dew from an agent',
  },
];

/** The features of one group, in the order they are declared. */
export const featuresIn = (group: FeatureGroupId): readonly Feature[] =>
  features.filter((feature) => feature.group === group);
