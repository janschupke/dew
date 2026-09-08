import { ShotViewer } from '@/ui/ShotViewer';

/** The screenshots, and their real sizes.
 *
 *  Served at the size they were rendered: the static export has no optimiser,
 *  so the browser scales them down and a caption stays legible.
 *
 *  The size is PER SHOT. next/image reserves an aspect-ratio box before the
 *  file arrives, and the gallery is nearly square where the rest are 16:10.
 *  `shots.test.ts` reads the PNG headers and fails if this table and the files
 *  disagree. */
export const shotSizes = {
  'channel-rack': { width: 2880, height: 1800 },
  'piano-roll': { width: 2880, height: 1800 },
  playlist: { width: 2880, height: 1800 },
  mixer: { width: 2880, height: 1800 },
  score: { width: 2880, height: 1800 },
  gallery: { width: 2880, height: 3340 },
} as const;

export type ShotName = keyof typeof shotSizes;

/** The frame and the caption stay on the server; only the button and the dialog
 *  are the client's. */
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
