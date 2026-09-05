import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it } from 'vitest';

import { Shot, shotSizes } from '@/ui/Shot';
import { t } from '@/lib/strings';

/*  The shot viewer, at the level this environment can see it.

    jsdom has no showModal, so tests/setup.ts supplies one and the first test
    here asserts that it did. Without that control this file would be green
    against a dialog that never opened, which is the shape of a gate that
    passes because it is blind.
*/
describe('a shot opens full-screen', () => {
  const open = (name: keyof typeof shotSizes = 'channel-rack') => {
    const view = render(<Shot name={name} alt="dew's channel rack" />);
    const button = screen.getByRole('button', { name: /channel rack/i });

    return { ...view, button, dialog: view.container.querySelector('dialog') };
  };

  it('has a dialog that can be opened at all', () => {
    // The control case. jsdom implements neither call; tests/setup.ts does.
    expect(typeof HTMLDialogElement.prototype.showModal).toBe('function');
    expect(typeof HTMLDialogElement.prototype.close).toBe('function');

    const element = document.createElement('dialog');

    element.showModal();
    expect(element.open).toBe(true);
    element.close();
    expect(element.open).toBe(false);
  });

  it('names the picture on the control that expands it', () => {
    // The button takes its accessible name from the image inside it, so a
    // reader hears which shot they are opening rather than "button".
    const { button } = open();

    expect(button).toHaveAttribute('title', t('shot.expand'));
    expect(button.querySelector('img')).not.toBeNull();
  });

  it('opens on a click and closes on the close control', () => {
    const { button, dialog } = open();

    expect(dialog).not.toBeNull();
    expect(dialog?.open).toBe(false);

    fireEvent.click(button);
    expect(dialog?.open).toBe(true);

    fireEvent.click(screen.getByRole('button', { name: t('shot.close') }));
    expect(dialog?.open).toBe(false);
  });

  it('closes on a click anywhere in it, the picture included', () => {
    // The whole viewer is one control. A shot that fills the screen leaves
    // almost no backdrop to aim at, so "click outside to close" would be a
    // target measured in pixels.
    const { button, dialog } = open();

    fireEvent.click(button);
    expect(dialog?.open).toBe(true);

    const inside = dialog?.querySelector('img');

    expect(inside).not.toBeNull();
    if (inside) fireEvent.click(inside);

    expect(dialog?.open).toBe(false);
  });

  it('does not announce the same picture twice', () => {
    // The dialog carries the description and its copy of the picture carries
    // none: it is the shot the reader just opened by name, and saying it again
    // adds nothing.
    const { dialog } = open();

    expect(dialog).toHaveAttribute('aria-label', "dew's channel rack");
    expect(dialog?.querySelector('img')).toHaveAttribute('alt', '');
  });

  it('reserves the size the file really is', () => {
    // `gallery` is 2880x3080 and the other five are 2880x1800, so a component
    // that reserved one box for every name would jump when the tall one landed.
    const { container } = render(<Shot name="gallery" alt="the design system" />);

    for (const image of container.querySelectorAll('img')) {
      expect(image.getAttribute('width')).toBe(String(shotSizes.gallery.width));
      expect(image.getAttribute('height')).toBe(String(shotSizes.gallery.height));
    }
  });
});
