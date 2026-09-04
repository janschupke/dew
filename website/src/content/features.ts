/** What dew does, for a reader deciding whether it is interesting.
 *
 *  Data rather than prose in a page, so a test can walk it and so the shape -
 *  a name, one sentence, an optional shot - is the same for all of them.
 *
 *  This is the site's own copy, and it is a SHORTER telling than README.md's
 *  "What it does". Per .ai/rules/README.md the README owns the reasoning and
 *  the contributor's tour; what is here is the claim, for somebody who has not
 *  cloned anything. Neither should restate the other's job.
 */
import type { ShotName } from '@/ui/Shot';

export interface Feature {
  readonly name: string;
  readonly body: string;

  /** A name from `shotSizes`, not any string: a shot the site names and
   *  `public/shots/` does not hold is a broken image, and this is the type that
   *  says so before a build. */
  readonly shot?: ShotName;
}

/** The id a feature's section carries on /features/, and the fragment the home
 *  page's cards link to.
 *
 *  Derived once here rather than written twice: the two pages split the same
 *  list on whether a feature has a shot, so a name typed out on one side and
 *  slugged on the other is six dead links nobody would notice. A test walks
 *  both directions.
 */
export const anchorForFeature = (name: string): string => name.toLowerCase().replace(/\s+/g, '-');

export const features: readonly Feature[] = [
  {
    name: 'Channel rack',
    body: 'A step grid, one row per channel. Volume, pan, mute and solo sit on the row itself, so a pattern is balanced where it is written.',
    shot: 'channel-rack',
  },
  {
    name: 'Piano roll',
    body: 'The same notes as the step grid, in a second view. Rubber-band select, move a chord without losing its shape, drag velocity bars in the lane below, click the keys to hear them.',
    shot: 'piano-roll',
  },
  {
    name: 'Playlist',
    body: 'Pattern clips on tracks along a bar timeline; a clip longer than its pattern repeats it. Lanes stretch, because an automation curve drawn into a 34px lane has a 7px grab radius.',
    shot: 'playlist',
  },
  {
    name: 'Mixer',
    body: 'A fader, pan, mute, solo and a peak meter per insert, plus master. Each strip lists the channels routed into it, and clicking one goes there.',
    shot: 'mixer',
  },
  {
    name: 'Score',
    body: 'A fifth tab holding the whole song as text: key, meter, chord progression, sections, and per channel a voicing, a melody or a counterpoint answering another voice. Checked as you type, compiled on demand.',
    shot: 'score',
  },
  {
    name: 'Instrument',
    body: 'Three band-limited oscillators or wavetables with unison, each with its own octave, detune, gain and switch. One ADSR behind them.',
  },
  {
    name: 'Effects',
    body: 'Reverb, filter, delay, drive, distortion, chorus, phaser, a 3-band EQ, a compressor and a limiter, chained up to four deep on any channel, track or the master. Drag a card by its grip to reorder; the whole drag is one undo step, however far it travelled.',
  },
  {
    name: 'Automation',
    body: 'Clips on the playlist driving a declared set of targets: channel and track volume and pan, master gain, and any effect parameter.',
  },
  {
    name: 'Recording',
    body: 'A channel that plays a recording instead of its oscillators. The microphone is asked for when you arm a channel, never at startup.',
  },
  {
    name: 'Render',
    body: 'The song, one pattern, or a span of bars, as WAV, FLAC, MP3 or MIDI, with sample rate, release tail, normalize, fades, dither and stems.',
  },
  {
    name: 'Time signature',
    body: '3/4, 5/4, 6/8, 7/8 and the rest, driving bar lines, beat shading, the readout, what "Bar" snaps to, and exported MIDI. A metre is not a tempo: a step is the same length in 3/4 as in 4/4.',
  },
  {
    name: 'It remembers',
    body: 'Window geometry, interface scale, active tab, selections, zoom, scroll, row height and snap. The piano roll’s tool does not: restoring into slice would mean the first click of a session cuts something nobody asked for.',
  },
];
