'use client';

import Image from 'next/image';
import { useRef } from 'react';

import { t } from '@/lib/strings';

/*  A shot, and the same shot filling the screen.

    The second client component on this site, after the nav, and for the reason
    the nav is one: this needs a ref and a handler, and there is no way to open
    a dialog from a server component.

    A NATIVE <dialog>, opened with showModal(), rather than a div with a high
    z-index. That one call buys the top layer - so it cannot lose to the pinned
    header's z-10 - plus Escape, a focus trap, and the rest of the page marked
    inert. Every one of those hand-rolled is a second implementation of
    something the platform already has, which is the trade this repository
    refuses everywhere else.

    THE WHOLE VIEWER IS ONE BUTTON, and clicking anywhere in it closes. The
    first version put the handler on the <dialog> and compared the click target
    against it, so that a click on the backdrop closed and a click on the
    picture did not - which is a mouse interaction on a non-interactive element,
    has no keyboard equivalent, and jsx-a11y refuses both. A picture that fills
    the screen leaves almost no backdrop to aim at anyway. So: click anywhere,
    or Escape, and the visible Close is a label on the same control rather than
    a second one.

    The picture is rendered twice, once in the button and once in the dialog. It
    is the same URL, so the second costs a cache hit and no request, and the
    alternative - moving one node between two parents - is a portal and a
    lifetime to go with it. The copy inside carries `alt=""`: it is the picture
    the reader just opened BY its description, and the dialog is labelled with
    that description, so announcing it twice would say nothing new.

    The sizing and the backdrop are in globals.css. They cannot be Tailwind
    classes: `fixed inset-0` is refused by tests/gates.test.ts, and refused for
    a good reason - `--spacing: initial` deletes the scale every numeric offset
    reads, so `inset-0` would emit nothing at all.
*/
export function ShotViewer({
  src,
  alt,
  width,
  height,
  priority,
}: {
  src: string;
  alt: string;
  width: number;
  height: number;
  priority: boolean;
}) {
  const dialog = useRef<HTMLDialogElement>(null);

  return (
    <>
      <button
        type="button"
        title={t('shot.expand')}
        onClick={() => dialog.current?.showModal()}
        className="block w-full cursor-zoom-in"
      >
        <Image
          src={src}
          alt={alt}
          width={width}
          height={height}
          priority={priority}
          className="h-auto w-full"
        />
      </button>

      <dialog ref={dialog} aria-label={alt} className="shot-viewer">
        <button
          type="button"
          onClick={() => dialog.current?.close()}
          className="p-gutter gap-md flex h-full w-full cursor-zoom-out flex-col"
        >
          <span className="bg-surface-raised text-primary border-outline px-stack py-md text-prose border-hairline self-end rounded-sm">
            {t('shot.close')}
          </span>

          <span className="flex min-h-0 w-full flex-1 items-center justify-center">
            <Image
              src={src}
              alt=""
              width={width}
              height={height}
              className="max-h-full w-auto max-w-full object-contain"
            />
          </span>
        </button>
      </dialog>
    </>
  );
}
