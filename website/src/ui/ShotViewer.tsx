'use client';

import Image from 'next/image';
import { useRef } from 'react';

import { t } from '@/lib/strings';

/*  A shot, and the same shot filling the screen.

    A native <dialog> opened with showModal(): that one call buys the top layer,
    Escape, a focus trap and an inert background, all of which hand-rolled would
    be a second implementation of something the platform has.

    The whole viewer is one button, so clicking anywhere in it closes and the
    visible Close is a label on the same control rather than a second one.

    Its sizing and backdrop are plain rules in globals.css: `fixed inset-0` is a
    numeric offset, which emits nothing under `--spacing: initial`.
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

          <span className="shrinkable flex w-full flex-1 items-center justify-center">
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
