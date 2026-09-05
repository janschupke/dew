import { ShotViewer } from '@/ui/ShotViewer';

/** A dew_shot render, in a frame.
 *
 *  Rendered at --scale 2 by scripts/gen-shots.sh and served as it was written:
 *  the static export has no image optimiser, because the optimiser is a server.
 *  So the intrinsic size is the real one and the browser scales it down, which
 *  is what keeps an 11px caption legible.
 *
 *  `wellDeep` for the frame, which is what the app itself puts behind a grid or
 *  a timeline - so the picture sits in the surface it came from.
 *
 *  The SIZE is per shot, and has to be. next/image uses width and height to
 *  reserve an aspect-ratio box before the file arrives, and this component used
 *  to hand it 2880x1800 for every name. Five of the six are that. The sixth,
 *  `gallery`, is 2880x3080 - `dew_shot gallery` sizes itself to its laid-out
 *  content, so its height is whatever the design system currently needs - and
 *  the design page therefore reserved a 16:10 slot for an image nearly square,
 *  then jumped when it loaded.
 *
 *  `shots.test.ts` reads the PNG headers and fails if this table and the files
 *  disagree, which is the only way a number that describes a file somebody else
 *  regenerates stays true.
 */
export const shotSizes = {
  'channel-rack': { width: 2880, height: 1800 },
  'piano-roll': { width: 2880, height: 1800 },
  playlist: { width: 2880, height: 1800 },
  mixer: { width: 2880, height: 1800 },
  score: { width: 2880, height: 1800 },
  gallery: { width: 2880, height: 3340 },
} as const;

export type ShotName = keyof typeof shotSizes;

/** The frame and the caption stay here, on the server. Only the button and the
 *  dialog are the client's - see ShotViewer for why there is a client at all. */
export function Shot({
  name,
  alt,
  caption,
  priority = false,
  className = 'my-stack',
}: {
  name: ShotName;
  alt: string;
  caption?: string;
  priority?: boolean;
  className?: string;
}) {
  const { width, height } = shotSizes[name];

  return (
    <figure className={className}>
      <div className="border-hairline border-divider bg-well-deep overflow-hidden rounded-md">
        <ShotViewer
          src={`/shots/${name}.png`}
          alt={alt}
          width={width}
          height={height}
          priority={priority}
        />
      </div>

      {caption ? (
        <figcaption className="mt-md text-fine text-secondary">{caption}</figcaption>
      ) : null}
    </figure>
  );
}
