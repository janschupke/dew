import Image from 'next/image';

/** A dew_shot render, in a frame.
 *
 *  Rendered at --scale 2 by scripts/gen-shots.sh and served as it was written:
 *  the static export has no image optimiser, because the optimiser is a server.
 *  So the intrinsic size is the real one and the browser scales it down, which
 *  is what keeps an 11px caption legible.
 *
 *  `wellDeep` for the frame, which is what the app itself puts behind a grid or
 *  a timeline - so the picture sits in the surface it came from.
 */
export function Shot({
  name,
  alt,
  caption,
  priority = false,
}: {
  name: string;
  alt: string;
  caption?: string;
  priority?: boolean;
}) {
  return (
    <figure className="my-xl">
      <div className="border-hairline border-divider bg-well-deep overflow-hidden rounded-md">
        <Image
          src={`/shots/${name}.png`}
          alt={alt}
          width={2880}
          height={1800}
          priority={priority}
          className="h-auto w-full"
        />
      </div>

      {caption ? (
        <figcaption className="mt-sm text-small text-secondary">{caption}</figcaption>
      ) : null}
    </figure>
  );
}
